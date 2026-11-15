#include "hongguoprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/qcjson.h"
#include "common/hgapp/hgapp.h"
#include "common/hgstream/hgstream.h"

#include <QUrlQuery>
#include <functional>

// 红果短剧 web 公开口(2026-11 实测口径,docs/protocols/bytedance.md §1.1):
// 解析一律 cJSON(qcjson);detail 的 _ROUTER_DATA 用
// cJSON_ParseWithLengthOpts(require_null_terminated=0)从 HTML 内截取。
// 风控:VIP 集 403 {"error":"public_fallback_unavailable"} → Paywalled。
// 19 位 ID 一律字符串(禁浮点/禁 toInt)。

namespace qxv {

static const char* kMod = "hongguo";
static const char* kSite = "https://hongguoduanju.com";

void HongguoProvider::webGet(const QString& path, const QString& query,
                             std::function<void(const NetResponse&)> cb) {
    static NetClient net;
    static CookieJar jar;
    static bool jarInit = (net.setCookieJar(&jar), true);
    NetRequest req;
    req.url = qFromUtf8("%1%2?%3").arg(kSite, path, query);
    req.headers = Fingerprint::chromeWeb(qFromUtf8("zh-CN,en-US")).headers;
    req.headers.append(qMakePair(qFromUtf8("Referer"), qFromUtf8(kSite)));
    req.autoCookie = true;
    req.timeoutSec = 25;
    // 官网口直连;代理仅显式 setProxy 时走(无墙)
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    DBG_P(kMod, Debug, "GET %s", qPrintable(req.url));
    net.send(req, std::move(cb));
}

void HongguoProvider::webPostJson(const QString& path, const QByteArray& body,
                                  const QString& referer,
                                  std::function<void(const NetResponse&)> cb) {
    static NetClient net;
    static CookieJar jar;
    static bool jarInit = (net.setCookieJar(&jar), true);
    NetRequest req;
    req.url = qFromUtf8("%1%2").arg(kSite, path);
    req.method = qFromUtf8("POST");
    req.body = body;
    req.headers = {
        { "Content-Type", "application/json; charset=UTF-8" },
        { "Origin", kSite },
    };
    if (!referer.isEmpty())
        req.headers.append({ "Referer", referer });
    req.headers.append(Fingerprint::chromeWeb(qFromUtf8("zh-CN,en-US"))
                           .headers.value(0));
    req.autoCookie = true;
    req.timeoutSec = 25;
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    DBG_P(kMod, Debug, "POST %s (%d bytes)", qPrintable(path), body.size());
    net.send(req, std::move(cb));
}

// ── search ──────────────────────────────────────────────────────────
// {"searchList":[{name,keyword,video_data:{series_id,series_title,
//   series_cover,series_intro,episode_cnt,pay_type,vid_list[前5],
//   category_list[{name}],celebrities,hot_score_data}}]}
void HongguoProvider::search(const QString& kw, int page, SearchCb cb) {
    Q_UNUSED(page);   // P1:首页 10 条;翻页参数待侦察(P2)
    QUrlQuery q;
    q.addQueryItem("query", kw);
    webGet("/api/search/result", q.toString(QUrl::FullyEncoded),
        [cb, kw](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        cJSON* list = jarr(root.p, "searchList");
        QList<VideoItem> items;
        for (int i = 0; i < jsize(list); ++i) {
            const cJSON* it = jat(list, i);
            const cJSON* vd = jobj(it, "video_data");
            VideoItem vi;
            vi.id    = jstr(vd, "series_id");
            vi.title = jstr(vd, "series_title");
            if (vi.title.isEmpty()) vi.title = jstr(it, "name");
            vi.cover = jstr(vd, "series_cover");
            // 主演取前两名为 author 形态(允许空)
            cJSON* celebs = jarr(vd, "celebrities");
            QString authors;
            for (int k = 0; k < jsize(celebs) && k < 2; ++k) {
                const QString nm = jstr(jat(celebs, k), "name");
                if (!nm.isEmpty())
                    authors = authors.isEmpty() ? nm : authors + "/" + nm;
            }
            vi.author = authors;
            QStringList cats;
            cJSON* cls = jarr(vd, "category_list");
            for (int k = 0; k < jsize(cls) && k < 3; ++k)
                cats << jstr(jat(cls, k), "name");
            vi.extra.insert(qFromUtf8("intro"), jstr(vd, "series_intro"));
            vi.extra.insert(qFromUtf8("episodeCnt"), (long long)jint(vd, "episode_cnt"));
            vi.extra.insert(qFromUtf8("payType"), (long long)jint(vd, "pay_type"));
            vi.extra.insert(qFromUtf8("category"), cats.join(qFromUtf8(",")));
            if (!vi.id.isEmpty()) items.append(vi);
        }
        DBG_P(kMod, Debug, "search '%s' -> %d items",
              qPrintable(kw), items.size());
        cb(Result::ok(), items, false);
    });
}

// ── detail ──────────────────────────────────────────────────────────
// HTML 内嵌 window._ROUTER_DATA={loaderData:{detail_page:{seriesDetail:
//   {series_id,series_name,series_cover,series_intro,episode_cnt,
//    accessible_episode_cnt,pay_type,vid_list[全集],tags,celebrities}}}}
static bool extractRouterData(const QByteArray& html, JRoot& out) {
    const QByteArray key("_ROUTER_DATA");
    const int at = html.indexOf(key);
    if (at < 0) return false;
    const char* start = html.constData() + at + key.size();
    const size_t left = static_cast<size_t>(html.size() - at - key.size());
    // 跳过到 '=' 之后第一个 '{'
    int off = 0;
    while (off < (int)left && start[off] != '{') ++off;
    if (off >= (int)left) return false;
    out.p = cJSON_ParseWithLengthOpts(start + off, left - (size_t)off,
                                      nullptr, 0);
    return out.ok();
}

void HongguoProvider::detail(const QString& id, DetailCb cb) {
    QUrlQuery q;
    q.addQueryItem("series_id", id);
    webGet("/detail", q.toString(QUrl::FullyEncoded),
        [cb, id](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, {}); return; }
        JRoot root;
        if (!extractRouterData(r.body, root)) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("hongguo: no _ROUTER_DATA in html")), {}, {});
            return;
        }
        const cJSON* sd = jobj(jobj(jobj(root.p, "loaderData"),
                                    "detail_page"), "seriesDetail");
        if (!sd) { cb(Result::fail(Err::Parse,
                    qFromUtf8("hongguo: no seriesDetail")), {}, {}); return; }
        VideoItem vi;
        vi.id    = jstr(sd, "series_id");
        if (vi.id.isEmpty()) vi.id = id;
        vi.title = jstr(sd, "series_name");
        vi.cover = jstr(sd, "series_cover");
        const long long accessible = jint(sd, "accessible_episode_cnt", -1);
        QList<VideoItem> parts;
        cJSON* vids = jarr(sd, "vid_list");
        for (int i = 0; i < jsize(vids); ++i) {
            VideoItem part;
            part.id = jstrVal(jat(vids, i));
            part.title = qFromUtf8("第%1集").arg(i + 1);
            part.extra.insert(qFromUtf8("seriesId"), vi.id);
            // 试看集外的分集打标(UI 可提前灰化;resolve 仍会 Paywalled)
            part.extra.insert(qFromUtf8("free"),
                              accessible >= 0 && i < accessible);
            parts.append(part);
        }
        Result res = Result::ok();
        res.detail.insert(qFromUtf8("accessibleEpisodeCnt"), accessible);
        res.detail.insert(qFromUtf8("payType"), (long long)jint(sd, "pay_type"));
        QStringList tags;
        cJSON* tg = jarr(sd, "tags");
        for (int i = 0; i < jsize(tg); ++i) tags << jstrVal(jat(tg, i));
        vi.extra.insert(qFromUtf8("intro"), jstr(sd, "series_intro"));
        vi.extra.insert(qFromUtf8("tags"), tags.join(qFromUtf8(",")));
        vi.extra.insert(qFromUtf8("episodeCnt"), (long long)jint(sd, "episode_cnt"));
        DBG_P(kMod, Debug, "detail %s -> %d episodes (free %lld)",
              qPrintable(id), parts.size(), accessible);
        cb(res, vi, parts);
    });
}

// ── resolve ─────────────────────────────────────────────────────────
// 入参 = series_id(detail 分集) → 内部 detail 换首集 vid
// 也接受 series_id@vid 直指某集(detail 的分集 id 即 vid)
// 取流顺序:① App 通道 1080p(bytevc1/H2a/H2b 本地解密代理)
//           ② web 公开口 360p 明文(fallback-play-info)
// App 链失败(风控/只有 bytevc2/代理起不来)一律显式回退 web,不静默失败。
HongguoProvider::~HongguoProvider() {
    if (!m_streamVid.isEmpty())
        hgstream::Server::instance().release(m_streamVid, m_streamQuality);
}

void HongguoProvider::resolve(const QString& id, ResolveCb cb) {
    QString seriesId = id, vid;
    const int at = id.indexOf('@');
    if (at > 0) { seriesId = id.left(at); vid = id.mid(at + 1); }

    auto playInfo = [this, cb, seriesId](const QString& vid2) {
        // body 构造用 cJSON(自动转义)
        cJSON* bodyRoot = cJSON_CreateObject();
        cJSON_AddStringToObject(bodyRoot, "item_id",
                                vid2.toUtf8().constData());
        char* s = cJSON_PrintUnformatted(bodyRoot);
        const QByteArray body(s);
        cJSON_free(s);
        cJSON_Delete(bodyRoot);

        webPostJson("/api/video/protected/fallback-play-info", body,
            qFromUtf8("https://hongguoduanju.com/player/%1/%2")
                .arg(seriesId, vid2),
            [cb, seriesId, vid2](const NetResponse& r) {
            if (r.status == 400 || r.status == 403) {
                // 403/400 public_fallback_unavailable = 试看集外(VIP/付费墙,
                // 两者实测均为该错误体,39 字节);不重试
                cb(Result::fail(Err::Paywalled,
                   qFromUtf8("hongguo: 试看集外(HTTP %1)").arg(r.status)), {});
                return;
            }
            if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}); return; }
            JRoot root(r.body);
            if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}); return; }
            const QString url = jstr(root.p, "main_url");
            if (url.isEmpty()) {
                const QString msg = jstr(jobj(root.p, "base_resp"),
                                         "StatusMessage");
                cb(Result::fail(Err::Parse,
                   qFromUtf8("hongguo: no main_url %1").arg(msg)), {});
                return;
            }
            PlayResult pr;
            pr.item.id = vid2;
            pr.item.extra.insert(qFromUtf8("seriesId"), seriesId);
            Stream st;
            st.url = url;
            st.quality = qFromUtf8("360P");   // 公开回退档固定
            st.kind = qFromUtf8("mp4");       // 明文渐进(fallback 口)
            st.extra.insert(qFromUtf8("urlExpire"),
                            (long long)jint(root.p, "url_expire"));
            cJSON* bk = jobj(root.p, "backup_url");
            if (!bk && jstr(root.p, "backup_url").isEmpty())
                bk = nullptr;
            const QString backup = jstr(root.p, "backup_url");
            if (!backup.isEmpty()) {
                Stream b = st;
                b.url = backup;
                pr.streams.append(b);
            }
            pr.streams.append(st);
            cb(Result::ok(), pr);
        });
    };

    // 通道②:App 协议(零签名 H3a)+ 本地流式解密代理(H2b)。
    // 只有 bytevc1(HEVC 通用可播)才走;bytevc2 私有编码 → web 保底并留说明。
    auto tryApp = [this, cb, seriesId, playInfo](const QString& vid2) {
        hgapp::videoModel(vid2, [this, cb, seriesId, vid2, playInfo](
                                    const Result& r,
                                    const QList<hgapp::Track>& tracks) {
            if (r.code != Err::Ok) {
                DBG_P(kMod, Info, "app chain failed (%s) -> web fallback",
                      qPrintable(r.message));
                playInfo(vid2);
                return;
            }
            const hgapp::Track* best = hgapp::pickBest(tracks);
            if (!best || !best->playable()) {
                DBG_P(kMod, Info, "app chain: only bytevc2 -> web fallback");
                playInfo(vid2);
                return;
            }
            QString localUrl;
            const Result o = hgstream::Server::instance().open(
                vid2, best->definition, best->url, best->spadeA, &localUrl);
            if (o.code != Err::Ok) {
                DBG_P(kMod, Warn, "local proxy failed (%s) -> web fallback",
                      qPrintable(o.message));
                playInfo(vid2);
                return;
            }
            // 切集:释放上一集会话(引用计数,避免会话累积)
            if (!m_streamVid.isEmpty())
                hgstream::Server::instance().release(m_streamVid,
                                                     m_streamQuality);
            m_streamVid = vid2;
            m_streamQuality = best->definition;

            PlayResult pr;
            pr.item.id = vid2;
            pr.item.extra.insert(qFromUtf8("seriesId"), seriesId);
            Stream st;
            st.url = localUrl;
            st.quality = best->definition.toUpper();   // "1080P"
            st.kind = qFromUtf8("mp4");                // 本地代理出明文 mp4
            st.extra.insert(qFromUtf8("localProxy"), true);
            st.extra.insert(qFromUtf8("cenc"), true);
            st.extra.insert(qFromUtf8("source"), qFromUtf8("app"));
            st.extra.insert(qFromUtf8("codec"), best->codecType);
            st.extra.insert(qFromUtf8("definition"), best->definition);
            pr.streams.append(st);
            DBG_P(kMod, Info, "resolve %s -> app %s (local proxy %s)",
                  qPrintable(vid2), qPrintable(best->definition),
                  qPrintable(localUrl));
            cb(Result::ok(), pr);
        });
    };

    if (!vid.isEmpty()) { tryApp(vid); return; }
    detail(seriesId, [cb, tryApp = std::move(tryApp)](
                         Result res, const VideoItem&,
                         const QList<VideoItem>& parts) {
        if (res.code != Err::Ok || parts.isEmpty()) { cb(res, {}); return; }
        tryApp(parts.first().id);   // 首集
    });
}

} // namespace qxv
