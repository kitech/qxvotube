#include "netflixprovider.h"
#include "common/qcjson.h"
#include "common/debuglog.h"
#include "netflix_data.h"

#include <functional>

// uNoGS(RapidAPI):仅目录元数据(标题/年份/类型/海报/各国可用性),无播放流。
// key 可选:qenv("QXVOTUBE_UNOGS_KEY") 为空 → 全部走本地数据集,不报错。
// 在线失败(网络/key 失效/额度用尽)→ 同一请求回退本地,用户无感。
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

// uNoGS title → VideoItem(缺失字段留 0/空,不造假)
static VideoItem itemFromJ(const cJSON* o)
{
    VideoItem it;
    if (!o) return it;
    it.id    = jstr(o, "nfid");
    if (it.id.isEmpty()) it.id = jstr(o, "id");
    it.title = jstr(o, "title");
    it.cover = jstr(o, "boxart");                  // Netflix CDN 海报 URL
    const QString y = jstr(o, "year");
    it.publishMs = y.isEmpty() ? 0 : (qint64)y.toInt() * 365 * 24 * 3600 * 1000;
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
    it.cover      = QString();                     // 本地集无封面 → UI 占位
    it.durationMs = 0;
    it.publishMs  = t.year.isEmpty() ? 0 : (qint64)t.year.toInt() * 365 * 24 * 3600 * 1000;
    it.webUrl     = qFromUtf8("https://www.netflix.com/title/%1").arg(t.id);
    it.extra.insert(qFromUtf8("source"), qFromUtf8("local"));
    it.extra.insert(qFromUtf8("genres"), t.genres);
    it.extra.insert(qFromUtf8("rating"), t.rating);
    it.extra.insert(qFromUtf8("country"), t.country);
    return it;
}

// 本地 search/detail 的公共过滤(大小写不敏感,复用 qToLower)
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

// 本地分页切片(per=20,统一 homeFeed/categoryFeed 回退路径)
static void localPaged(const QList36<VideoItem>& all, int page, SearchCb cb)
{
    const int per = 20, from = (page - 1) * per;
    QList36<VideoItem> slice;
    for (int i = from; i < from + per && i < all.size(); ++i)
        slice.append(all[i]);
    cb(Result::ok(), slice, from + per < all.size());
}

// categories() —— 有 key 拉 /genres,失败/无 key 用本地派生
void NetflixProvider::categories(
    std::function<void(const Result&, const QList36<CategoryItem>&)> cb)
{
    if (!hasKey()) {
        QList36<CategoryItem> cats;
        cats.append(CategoryItem(QString(), qFromUtf8("推荐"))); // 空 key = homeFeed
        const QStringList gs = netflixLocalGenres();
        for (int i = 0; i < gs.size(); ++i)
            cats.append(CategoryItem(gs[i], gs[i]));
        cb(Result::ok(), cats);
        return;
    }
    unogGet("/genres", QString(), [cb](const NetResponse& r) {
        QList36<CategoryItem> cats;
        cats.append(CategoryItem(QString(), qFromUtf8("推荐")));
        if (r.ok()) {
            JRoot root(r.body);
            cJSON* arr = jarr(root.p, "results");
            if (root.ok() && arr) {
                for (int i = 0; i < jsize(arr); ++i) {
                    cJSON* o = jat(arr, i);
                    QString g = jstr(o, "genre");
                    if (g.isEmpty() && cJSON_IsString(o)) g = jstrVal(o);
                    if (!g.isEmpty()) cats.append(CategoryItem(g, g));
                }
            }
        }
        // 在线失败或解析空 → 本地兜底(不把"无 key/额度尽"当用户错误)
        if (cats.size() <= 1) {
            const QStringList gs = netflixLocalGenres();
            for (int i = 0; i < gs.size(); ++i)
                if (!gs[i].isEmpty())
                    cats.append(CategoryItem(gs[i], gs[i]));
        }
        cb(Result::ok(), cats);
    });
}

// homeFeed() —— 有 key 拉 /search(按日期),失败/无 key 用本地全集
void NetflixProvider::homeFeed(int page, SearchCb cb)
{
    if (!hasKey()) {
        localPaged(localSearch(QString()), page, cb);
        return;
    }
    QUrlQuery q;
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number((page - 1) * 20));
    q.addQueryItem("orderby", "date");
    unogGet("/search", q.toString(), [page, cb](const NetResponse& r) {
        if (r.ok()) {
            JRoot root(r.body);
            cJSON* arr = jarr(root.p, "results");
            if (root.ok() && arr) {
                QList36<VideoItem> items;
                for (int i = 0; i < jsize(arr); ++i) {
                    VideoItem it = itemFromJ(jat(arr, i));
                    if (!it.id.isEmpty()) items.append(it);
                }
                if (!items.isEmpty()) {
                    cb(Result::ok(), items, jsize(arr) == 20);
                    return;
                }
            }
        }
        localPaged(localSearch(QString()), page, cb);
    });
}

// categoryFeed() —— key 为 genre;空 key = 推荐。在线用 genre 参数,回退本地子串过滤
void NetflixProvider::categoryFeed(const QString& key, int page, SearchCb cb)
{
    if (key.isEmpty()) { homeFeed(page, cb); return; }
    if (!hasKey()) {
        QList36<VideoItem> filtered;
        const QList36<NfTitle>& all = netflixLocalTitles();
        for (int i = 0; i < all.size(); ++i)
            if (all[i].genres.contains(key))
                filtered.append(itemFromLocal(all[i]));
        localPaged(filtered, page, cb);
        return;
    }
    QUrlQuery q;
    q.addQueryItem("genre", key);
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number((page - 1) * 20));
    unogGet("/search", q.toString(), [key, page, cb](const NetResponse& r) {
        if (r.ok()) {
            JRoot root(r.body);
            cJSON* arr = jarr(root.p, "results");
            if (root.ok() && arr) {
                QList36<VideoItem> items;
                for (int i = 0; i < jsize(arr); ++i) {
                    VideoItem it = itemFromJ(jat(arr, i));
                    if (!it.id.isEmpty()) items.append(it);
                }
                if (!items.isEmpty()) { cb(Result::ok(), items, jsize(arr) == 20); return; }
            }
        }
        QList36<VideoItem> filtered;
        const QList36<NfTitle>& all = netflixLocalTitles();
        for (int i = 0; i < all.size(); ++i)
            if (all[i].genres.contains(key))
                filtered.append(itemFromLocal(all[i]));
        localPaged(filtered, page, cb);
    });
}

// search() —— 在线用 /search?query=,本地按标题/类型/简介过滤
void NetflixProvider::search(const QString& kw, int page, SearchCb cb)
{
    if (!hasKey()) {
        cb(Result::ok(), localSearch(kw), false);
        return;
    }
    QUrlQuery q;
    q.addQueryItem("query", kw);
    q.addQueryItem("limit", "20");
    q.addQueryItem("offset", QString::number((page - 1) * 20));
    unogGet("/search", q.toString(), [kw, cb](const NetResponse& r) {
        if (r.ok()) {
            JRoot root(r.body);
            cJSON* arr = jarr(root.p, "results");
            if (root.ok() && arr) {
                QList36<VideoItem> items;
                for (int i = 0; i < jsize(arr); ++i) {
                    VideoItem it = itemFromJ(jat(arr, i));
                    if (!it.id.isEmpty()) items.append(it);
                }
                if (!items.isEmpty()) { cb(Result::ok(), items, jsize(arr) == 20); return; }
            }
        }
        cb(Result::ok(), localSearch(kw), false);
    });
}

// detail() —— 在线 /title?id=,本地按 id 查找
void NetflixProvider::detail(const QString& id, DetailCb cb)
{
    if (!hasKey()) {
        NfTitle t;
        if (netflixFindLocal(id, &t))
            cb(Result::ok(), itemFromLocal(t), QList36<VideoItem>());
        else
            cb(Result::fail(Err::NotFound, qFromUtf8("未找到该条目")),
               VideoItem(), QList36<VideoItem>());
        return;
    }
    QUrlQuery q;
    q.addQueryItem("id", id);
    unogGet("/title", q.toString(), [id, cb](const NetResponse& r) {
        if (r.ok()) {
            JRoot root(r.body);
            cJSON* arr = jarr(root.p, "results");
            if (root.ok() && arr && jsize(arr) > 0) {
                cb(Result::ok(), itemFromJ(jat(arr, 0)), QList36<VideoItem>());
                return;
            }
        }
        NfTitle t;
        if (netflixFindLocal(id, &t))
            cb(Result::ok(), itemFromLocal(t), QList36<VideoItem>());
        else
            cb(Result::fail(Err::NotFound, qFromUtf8("未找到该条目")),
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
