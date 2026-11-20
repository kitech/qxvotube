#include "netflixprovider.h"
#include "common/qcjson.h"
#include "common/debuglog.h"
#include "netflix_data.h"

#include <functional>

// uNoGS(RapidAPI):仅目录元数据(标题/年份/类型/海报/各国可用性),无播放流。
// 分类/列表/详情都是「实时拉取 + 本地合并」:始终发请求(无 key 发空 key → 401 入日志),
// 在线结果与本地数据集按 id 去重组合展示;在线失败只记日志,本地半边照常显示。
// 解析一律 cJSON(JRoot),禁用 QJson(Qt3 兼容)。

namespace qxv {

static const char* kMod = "netflix";
static const char* kApi = "https://unogsng.p.rapidapi.com";

bool NetflixProvider::hasKey() const
{
    return !qenv("QXVOTUBE_UNOGS_KEY").isEmpty();
}

void NetflixProvider::unogGet(const QString& path, const QString& query,
                              std::function<void(const NetResponse&)> cb)
{
    NetRequest req;
    req.url = qFromUtf8("%1%2?%3").arg(qFromUtf8(kApi), path, query);
    req.headers.append(qMakePair(qFromUtf8("x-rapidapi-key"),
                                 qenv("QXVOTUBE_UNOGS_KEY")));
    req.headers.append(qMakePair(qFromUtf8("x-rapidapi-host"),
                                 qFromUtf8("unogsng.p.rapidapi.com")));
    req.timeoutSec = 20;
    req.proxy = effectiveProxy(m_proxy, mustProxy(), kMod);
    DBG_P(kMod, Info, "GET %s", qToUtf8(req.url).data());
    m_net.send(req, std::move(cb));
}

// uNoGS/RapidAPI 失败响应 → Result(实测错误码/错误串见 docs/providers/protocols/netflix.md):
//   401 Invalid API key / 403 You are not subscribed → AuthExpired
//   429 Too many requests                            → RateLimit
//   其它非 2xx                                       → Http
//   curl 层错误(r.error 非空)                        → Network
static Result apiErr(const NetResponse& r, const QString& where)
{
    QString bodyMsg;
    if (!r.body.isEmpty()) {
        JRoot root(r.body);                 // {"message":"..."}
        bodyMsg = jstr(root.p, "message");
    }
    const QString msg = qFromUtf8("%1: HTTP %2 %3")
                          .arg(where).arg(r.status)
                          .arg(bodyMsg.isEmpty() ? r.error : bodyMsg);
    QVariantMap detail;
    detail.insert(qFromUtf8("httpStatus"), r.status);
    detail.insert(qFromUtf8("platform"), qFromUtf8("unogsng"));
    Err e;
    if (!r.error.isEmpty()) e = Err::Network;
    else if (r.status == 401 || r.status == 403) e = Err::AuthExpired;
    else if (r.status == 429) e = Err::RateLimit;
    else e = Err::Http;
    DBG_P(kMod, Warn, "%s", qToUtf8(msg).data());
    return Result::fail(e, msg, detail);
}

// 在线失败时的"软失败":错误码/错误串已由 apiErr 写入日志与 detail,
// 但对外返回 Ok —— UI(mainwindow.cpp)在 result!=Ok 时会丢弃整个 items,
// 返回 Ok 才能让"本地 ∪ 实时"里的本地部分照常显示(分类/列表/详情)。
static Result softFail(const NetResponse& r, const QString& where)
{
    Result e = apiErr(r, where);
    e.code = Err::Ok;               // 保留 message/detail,仅放开 UI 渲染
    return e;
}

// 年份 → epoch 毫秒(1970..year 逐累加天数,含闰年);非法返回 0。
// 修复 UI 用 (uint)(publishMs/1000) 截断导致 2016 显示成 2080/2066。
static qint64 yearToMs(const QString& y)
{
    const int yy = y.toInt();
    if (yy < 1970 || yy > 2100) return 0;
    qint64 days = 0;
    for (int i = 1970; i < yy; ++i)
        days += ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)) ? 366 : 365;
    return days * 86400LL * 1000LL;
}

// uNoGS title → VideoItem(缺失字段留 0/空,不造假)
static VideoItem itemFromJ(const cJSON* o)
{
    VideoItem it;
    if (!o) return it;
    it.id    = jstr(o, "nfid");
    if (it.id.isEmpty()) it.id = jstr(o, "id");
    it.title = jstr(o, "title");
    it.cover = jstr(o, "boxart");                  // Netflix CDN 海报 URL
    it.publishMs = yearToMs(jstr(o, "year"));
    const QString d = jstr(o, "runtime");          // "45" / 秒数(容错按分钟)
    const int dm = d.toInt();
    it.durationMs = dm > 0 ? (qint64)dm * 60 * 1000 : 0;
    const QString nm = jstr(o, "director");
    it.author = nm.isEmpty() ? jstr(o, "type") : nm;
    it.webUrl = it.id.isEmpty() ? QString()
              : qFromUtf8("https://www.netflix.com/title/%1").arg(it.id);
    it.extra.insert(qFromUtf8("source"), qFromUtf8("unogs"));
    return it;
}

static VideoItem itemFromLocal(const NfTitle& t)
{
    VideoItem it;
    it.id         = t.id;
    it.title      = t.title;
    it.author     = t.type;                        // 本地集无导演,用类型占位
    it.cover      = QString();                     // 本地集无封面 → UI identicon 占位
    it.durationMs = 0;
    it.publishMs  = yearToMs(t.year);
    it.webUrl     = qFromUtf8("https://www.netflix.com/title/%1").arg(t.id);
    it.extra.insert(qFromUtf8("source"), qFromUtf8("local"));
    it.extra.insert(qFromUtf8("genres"), t.genres);
    it.extra.insert(qFromUtf8("rating"), t.rating);
    it.extra.insert(qFromUtf8("country"), t.country);
    return it;
}

// 本地 search 过滤(大小写不敏感,复用 qToLower)
static QList36<VideoItem> localSearch(const QString& kw)
{
    QList36<VideoItem> out;
    const QString needle = qToLower(kw);
    const QList36<NfTitle>& all = netflixLocalTitles();
    for (int i = 0; i < all.size(); ++i) {
        const NfTitle& t = all[i];
        if (needle.isEmpty()
            || qToLower(t.title).contains(needle)
            || qToLower(t.genres).contains(needle)
            || qToLower(t.desc).contains(needle))
            out.append(itemFromLocal(t));
    }
    return out;
}

// 本地按 genre 子串过滤(大小写不敏感)
static QList36<VideoItem> localByGenre(const QString& key)
{
    QList36<VideoItem> out;
    const QString needle = qToLower(key);
    const QList36<NfTitle>& all = netflixLocalTitles();
    for (int i = 0; i < all.size(); ++i)
        if (qToLower(all[i].genres).contains(needle))
            out.append(itemFromLocal(all[i]));
    return out;
}

// uNoGS results[] → VideoItem 列表(公共解析)
static QList36<VideoItem> itemsFromResp(const JRoot& root)
{
    QList36<VideoItem> items;
    cJSON* arr = jarr(root.p, "results");
    if (root.ok() && arr)
        for (int i = 0; i < jsize(arr); ++i) {
            VideoItem it = itemFromJ(jat(arr, i));
            if (!it.id.isEmpty()) items.append(it);
        }
    return items;
}

// 本地 genre → CategoryItem 列表(含"推荐"空 key 项)
static QList36<CategoryItem> localCats()
{
    QList36<CategoryItem> cats;
    cats.append(CategoryItem(QString(), qFromUtf8("推荐")));
    const QStringList gs = netflixLocalGenres();
    for (int i = 0; i < gs.size(); ++i)
        if (!gs[i].isEmpty()) cats.append(CategoryItem(gs[i], gs[i]));
    return cats;
}

// 在线当页结果 + 本地补充(按 page 推进窗口,每页 20 条,按 id 去重)。
// 在线用 offset 翻页,本地补充不参与在线切片,避免重复/漏项。
// 在线失败时 online 为空、res 为 softFail(错误已入日志),本函数仍回本地分页,
// 于是无 key 也能看到本地 400 条并滚动翻页。
// hasMore = 在线本页满 20 条(后端还有) || 本地补充窗口未到尾。
static void mergedPaged(const Result& res, const QList36<VideoItem>& online,
                        bool onlineFull, const QList36<VideoItem>& local,
                        int page, SearchCb cb)
{
    const int per = 20, from = (page - 1) * per;
    QList36<VideoItem> out = online;
    for (int i = from; i < from + per && i < local.size(); ++i) {
        bool dup = false;
        for (int j = 0; j < out.size(); ++j)
            if (!out[j].id.isEmpty() && out[j].id == local[i].id) { dup = true; break; }
        if (!dup) out.append(local[i]);
    }
    cb(res, out, onlineFull || (from + per < local.size()));
}

// 无 key 时打一条 Warn,但仍照常发请求(让 API 返回 401 进报错日志链路)
static void warnNoKey()
{
    DBG_P(kMod, Warn, "QXVOTUBE_UNOGS_KEY 未设置,将以空 key 请求(预期 401)");
}

// categories() —— 立即同步返回完整本地分类(推荐 + 全部本地 genre),不等待网络。
// 在线 /genres 的增量合并交给 categoriesRefresh() 后台完成,UI 收到后就地刷新 chips,
// 这样本地分类秒显,不被在线 401 往返拖住。
void NetflixProvider::categories(
    std::function<void(const Result&, const QList36<CategoryItem>&)> cb)
{
    cb(Result::ok(), localCats());
}

// categoriesRefresh() —— 后台拉 /genres,成功后在线优先去重合并不再重复本地全量
// (本地那半边 categories() 已显示)。在线优先:首项恒为「推荐」,在线 genre 在前,
// 本地仅补在线没有的。失败 → softFail 记日志、不回调(保持已显示的本地分类)。
void NetflixProvider::categoriesRefresh(
    std::function<void(const Result&, const QList36<CategoryItem>&)> cb)
{
    if (!hasKey()) warnNoKey();
    unogGet("/genres", QString(), [cb](const NetResponse& r) {
        if (!r.ok()) { softFail(r, qFromUtf8("unogs /genres")); return; }
        JRoot root(r.body);
        cJSON* arr = jarr(root.p, "results");
        if (!root.ok() || !arr) {
            DBG_P(kMod, Warn, "unogs /genres 解析失败,保持本地分类");
            softFail(r, qFromUtf8("unogs /genres 解析失败"));
            return;
        }
        QList36<CategoryItem> cats;
        cats.append(CategoryItem(QString(), qFromUtf8("推荐"))); // 空 key = homeFeed
        for (int i = 0; i < jsize(arr); ++i) {
            cJSON* o = jat(arr, i);
            QString g = jstr(o, "genre");
            if (g.isEmpty() && cJSON_IsString(o)) g = jstrVal(o);
            if (g.isEmpty()) continue;
            bool dup = false;
            for (int j = 0; j < cats.size(); ++j)
                if (cats[j].key == g) { dup = true; break; }
            if (!dup) cats.append(CategoryItem(g, g));       // 在线在前
        }
        const QStringList local = netflixLocalGenres();
        for (int i = 0; i < local.size(); ++i) {            // 本地仅补在线没有的
            if (local[i].isEmpty()) continue;
            bool dup = false;
            for (int j = 0; j < cats.size(); ++j)
                if (cats[j].key == local[i]) { dup = true; break; }
            if (!dup) cats.append(CategoryItem(local[i], local[i]));
        }
        cb(Result::ok(), cats);
    });
}

// homeFeed() —— 实时拉 /search(按日期) + 本地全集合并;失败只记日志仍回本地分页
void NetflixProvider::homeFeed(int page, SearchCb cb)
{
    if (!hasKey()) warnNoKey();
    QUrlQuery q;
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number((page - 1) * 20));
    q.addQueryItem("orderby", "date");
    unogGet("/search", q.toString(), [page, cb](const NetResponse& r) {
        const QList36<VideoItem> local = localSearch(QString());
        if (!r.ok()) {
            mergedPaged(softFail(r, qFromUtf8("unogs /search")),
                        QList36<VideoItem>(), false, local, page, cb);
            return;
        }
        JRoot root(r.body);
        cJSON* arr = jarr(root.p, "results");
        const bool full = arr && jsize(arr) == 20;
        QList36<VideoItem> online = itemsFromResp(root);
        Result res = online.isEmpty()
                   ? softFail(r, qFromUtf8("unogs /search 解析空")) : Result::ok();
        mergedPaged(res, online, full, local, page, cb);
    });
}

// categoryFeed() —— key 为 genre;空 key = 推荐。实时拉 + 本地同类合并;失败仍回本地
void NetflixProvider::categoryFeed(const QString& key, int page, SearchCb cb)
{
    if (key.isEmpty()) { homeFeed(page, cb); return; }
    if (!hasKey()) warnNoKey();
    QUrlQuery q;
    q.addQueryItem("genre", key);
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number((page - 1) * 20));
    unogGet("/search", q.toString(), [key, page, cb](const NetResponse& r) {
        const QList36<VideoItem> local = localByGenre(key);
        if (!r.ok()) {
            mergedPaged(softFail(r, qFromUtf8("unogs /search")),
                        QList36<VideoItem>(), false, local, page, cb);
            return;
        }
        JRoot root(r.body);
        cJSON* arr = jarr(root.p, "results");
        const bool full = arr && jsize(arr) == 20;
        QList36<VideoItem> online = itemsFromResp(root);
        Result res = online.isEmpty()
                   ? softFail(r, qFromUtf8("unogs /search 解析空")) : Result::ok();
        mergedPaged(res, online, full, local, page, cb);
    });
}

// search() —— 实时拉 /search?query= + 本地匹配合并;失败仍回本地匹配
void NetflixProvider::search(const QString& kw, int page, SearchCb cb)
{
    if (!hasKey()) warnNoKey();
    QUrlQuery q;
    q.addQueryItem("query", kw);
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number((page - 1) * 20));
    unogGet("/search", q.toString(), [kw, page, cb](const NetResponse& r) {
        const QList36<VideoItem> local = localSearch(kw);
        if (!r.ok()) {
            mergedPaged(softFail(r, qFromUtf8("unogs /search")),
                        QList36<VideoItem>(), false, local, page, cb);
            return;
        }
        JRoot root(r.body);
        cJSON* arr = jarr(root.p, "results");
        const bool full = arr && jsize(arr) == 20;
        QList36<VideoItem> online = itemsFromResp(root);
        Result res = online.isEmpty()
                   ? softFail(r, qFromUtf8("unogs /search 解析空")) : Result::ok();
        mergedPaged(res, online, full, local, page, cb);
    });
}

// detail() —— 实时拉 /title?id= + 本地同 id 合并(在线优先);失败回本地详情
void NetflixProvider::detail(const QString& id, DetailCb cb)
{
    if (!hasKey()) warnNoKey();
    QUrlQuery q;
    q.addQueryItem("id", id);
    unogGet("/title", q.toString(), [id, cb](const NetResponse& r) {
        NfTitle t;
        const bool hasLocal = netflixFindLocal(id, &t);
        if (!r.ok()) {
            if (hasLocal)
                cb(softFail(r, qFromUtf8("unogs /title")),
                   itemFromLocal(t), QList36<VideoItem>());
            else
                cb(apiErr(r, qFromUtf8("unogs /title")),
                   VideoItem(), QList36<VideoItem>());
            return;
        }
        JRoot root(r.body);
        QList36<VideoItem> items = itemsFromResp(root);
        if (!items.isEmpty()) {
            cb(Result::ok(), items[0], QList36<VideoItem>());   // 在线优先
            return;
        }
        if (hasLocal) {
            cb(softFail(r, qFromUtf8("unogs /title 解析空")),
               itemFromLocal(t), QList36<VideoItem>());
            return;
        }
        cb(Result::fail(Err::NotFound, qFromUtf8("unogs /title 未找到该条目")),
           VideoItem(), QList36<VideoItem>());
    });
}

// 无免登录播放源:固定 Unsupported(UI 显示 error.unsupported),不静默给空流
void NetflixProvider::resolve(const QString& id, ResolveCb cb)
{
    Q_UNUSED(id);
    cb(Result::fail(Err::Unsupported,
        qFromUtf8("Netflix 为订阅制且受 DRM 保护,无免登录可播放源")), PlayResult());
}

void NetflixProvider::resolveQuality(const QString& id, const QString& quality,
                                     ResolveCb cb)
{
    Q_UNUSED(quality);
    resolve(id, std::move(cb));
}

} // namespace qxv
