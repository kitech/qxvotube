#include "hongguoprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/qcjson.h"
#include "parse.h"
#include "common/quality.h"
#include "common/autherror.h"
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

void HongguoProvider::cancel() {
    m_net.cancel();
    m_cache.clear();
}


static const char* kMod = "hongguo";
static const char* kSite = "https://hongguoduanju.com";

void HongguoProvider::webGet(const QString& path, const QString& query,
                             std::function<void(const NetResponse&)> cb) {
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
    NetRequest req;
    req.url = qFromUtf8("%1%2?%3").arg(kSite, path, query);
    req.headers = Fingerprint::chromeWeb(qFromUtf8("zh-CN,en-US")).headers;
    req.headers.append(qMakePair(qFromUtf8("Referer"), qFromUtf8(kSite)));
    req.autoCookie = true;
    req.timeoutSec = 25;
    // 官网口直连;代理仅显式 setProxy 时走(无墙)
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    req.maxAttempts = 3;
    req.retryDelayMs = 300;
    DBG_P(kMod, Debug, "GET %s", qPrintable(req.url));
    m_net.send(req, std::move(cb));
}

void HongguoProvider::webPostJson(const QString& path, const QByteArray& body,
                                  const QString& referer,
                                  std::function<void(const NetResponse&)> cb) {
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
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
    req.maxAttempts = 3;
    req.retryDelayMs = 300;
    DBG_P(kMod, Debug, "POST %s (%d bytes)", qPrintable(path), body.size());
    m_net.send(req, std::move(cb));
}

// ── search ──────────────────────────────────────────────────────────
// {"searchList":[{name,keyword,video_data:{series_id,series_title,
//   series_cover,series_intro,episode_cnt,pay_type,vid_list[前5],
//   category_list[{name}],celebrities,hot_score_data}}]}
void HongguoProvider::search(const QString& kw, int page, SearchCb cb) {
    QUrlQuery q;
    q.addQueryItem("query", kw);
    q.addQueryItem("page", QString::number(page));   // 实测支持;响应带分页元信息
    webGet("/api/search/result", q.toString(QUrl::FullyEncoded),
        [cb, kw, page](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        bool hasMore = false;
        const QList<VideoItem> items = hongguo::parse::searchList(root.p, page, &hasMore);
        DBG_P(kMod, Debug, "search '%s' p%d -> %d items (hasMore=%d)",
              qPrintable(kw), page, items.size(), int(hasMore));
        cb(Result::ok(), items, hasMore);
    });
}

// ── detail ──────────────────────────────────────────────────────────
// HTML 内嵌 window._ROUTER_DATA={loaderData:{detail_page:{seriesDetail:
//   {series_id,series_name,series_cover,series_intro,episode_cnt,
//    accessible_episode_cnt,pay_type,vid_list[全集],tags,celebrities}}}}

// ── homeFeed(官网首页热播,SSR)───────────────────────────────────────
// GET / → window._ROUTER_DATA.loaderData.page.homeSections[]
//   [{tab_type:"all"/"human"/"comic"/"ai", tab_name, video_list[]}]
// 每类 9 条;只有第 1 页(站点无分页参数)→ page>1 返回空 + hasMore=false。
void HongguoProvider::homeFeed(int page, SearchCb cb) {
    if (page > 1) {   // 第 2 页起走热播榜(/rank/hot-drama?page=N,20 条/页)
        webGet(qFromUtf8("/rank/hot-drama"),
               qFromUtf8("page=%1").arg(page),
            [cb, page](const NetResponse& r) {
            if (!r.ok()) {
                cb(Result::fail(Err::Network, r.error.isEmpty()
                       ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
                return;
            }
            JRoot root;
            if (!hongguo::parse::extractRouterData(r.body, root)) {
                cb(Result::fail(Err::Parse,
                   qFromUtf8("hongguo: 榜单页无 _ROUTER_DATA")), {}, false);
                return;
            }
            bool hasMore = false;
            const QList<VideoItem> items =
                hongguo::parse::rankList(root.p, page, &hasMore);
            DBG_P(kMod, Debug, "homeFeed(rank) p%d -> %d items (hasMore=%d)",
                  page, items.size(), int(hasMore));
            cb(Result::ok(), items, hasMore);
        });
        return;
    }
    webGet("/", QString(),
        [cb](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        JRoot root;
        if (!hongguo::parse::extractRouterData(r.body, root)) {
            cb(Result::fail(Err::Parse, qFromUtf8("hongguo: 首页无 _ROUTER_DATA")),
               {}, false);
            return;
        }
        const QList<VideoItem> items = hongguo::parse::homeSections(root.p);
        DBG_P(kMod, Debug, "homeFeed -> %d items", items.size());
        cb(Result::ok(), items, true);   // 第 2 页起走 /rank/hot-drama
    });
}

void HongguoProvider::detail(const QString& id, DetailCb cb) {
    QUrlQuery q;
    q.addQueryItem("series_id", id);
    webGet("/detail", q.toString(QUrl::FullyEncoded),
        [cb, id](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, {}); return; }
        JRoot root;
        if (!hongguo::parse::extractRouterData(r.body, root)) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("hongguo: no _ROUTER_DATA in html")), {}, {});
            return;
        }
        VideoItem vi;
        QList<VideoItem> parts;
        const Result dres = hongguo::parse::seriesDetail(root.p, &vi, &parts);
        DBG_P(kMod, Debug, "detail %s -> %d episodes", qPrintable(id),
              parts.size());
        // 预热前 N 集 tracks(默认 3;QXVOTUBE_HG_PREFETCH=0 关闭)
        if (dres.code == Err::Ok && !parts.isEmpty()) {
            int n = 3;
            if (!QXV_ENV_EMPTY("QXVOTUBE_HG_PREFETCH"))
                n = QXV_ENV("QXVOTUBE_HG_PREFETCH").toInt();
            QStringList vids;
            for (int i = 0; i < parts.size() && i < n; ++i)
                if (!parts[i].id.isEmpty()) vids << parts[i].id;
            if (!vids.isEmpty())
                hgapp::videoModels(vids, [](const Result& r,
                                            const QMap<QString, QList<hgapp::Track>>& m) {
                    DBG_P(kMod, Debug, "prefetch -> %d/%d (%s)", m.size(),
                          qPrintable(QString::number(1)), qPrintable(r.message));
                });
        }
        cb(dres, vi, parts);

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
    resolveQuality(id, QString(), std::move(cb));
}

void HongguoProvider::resolveQuality(const QString& id, const QString& quality,
                                     ResolveCb cb) {
    QString seriesId = id, vid;
    const int at = id.indexOf('@');
    if (at > 0) { seriesId = id.left(at); vid = id.mid(at + 1); }

    auto playInfo = [this, seriesId](const QString& vid2, ResolveCb cb2) {
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
            [cb2, seriesId, vid2](const NetResponse& r) {
            if (r.status == 400 || r.status == 403) {
                // 403/400 public_fallback_unavailable = 试看集外(VIP/付费墙,
                // 两者实测均为该错误体,39 字节);不重试
                cb2(withServer(
                    Result::fail(Err::Paywalled,
                        qFromUtf8("hongguo: 试看集外(HTTP %1)").arg(r.status)),
                    r.status, QString::fromUtf8("public_fallback_unavailable"),
                    QString::fromUtf8(r.body.left(200))), {});
                return;
            }
            if (!r.ok()) { cb2(Result::fail(Err::Network, r.error), {}); return; }
            JRoot root(r.body);
            if (!root.ok()) { cb2(Result::fail(Err::Parse, "not json"), {}); return; }
            const QString url = jstr(root.p, "main_url");
            if (url.isEmpty()) {
                const QString msg = jstr(jobj(root.p, "base_resp"),
                                         "StatusMessage");
                cb2(Result::fail(Err::Parse,
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
            cb2(Result::ok(), pr);
        });
    };

    // 直链/本地代理地址缓存:命中直接返回;会话被回收/过期时自动重开
    const QString ckey = qFromUtf8("hongguo|%1|%2").arg(id, quality);
    {
        PlayResult cached;
        if (m_cache.get(ckey, &cached)) {
            DBG_P(kMod, Debug, "resolve cache hit %s", qPrintable(id));
            cb(Result::ok(), cached);
            return;
        }
    }

    // 低清请求(≤720p):红果 App 侧全是 bytevc2 私有编码(不可播),
    // 改走官网明文通道(H1,360P,仅免费集);付费墙则显式说明。
    const int wantH = quality::heightOf(quality);
    if (wantH > 0 && wantH <= 720) {
        auto viaWeb = [this, cb, seriesId, quality, wantH,
                       playInfo](const QString& vid2) {
            playInfo(vid2, [cb, seriesId, quality, wantH](
                               Result r, PlayResult pr) {
                if (r.code == Err::Paywalled) {
                    cb(Result::fail(Err::Unsupported,
                       qFromUtf8("hongguo: ≤720p 仅官网免费集可用"
                                 "(App 侧为 bytevc2 私有编码不可播),"
                                 "请选 1080p")), {});
                    return;
                }
                if (r.code == Err::Ok) {
                    for (int i = 0; i < pr.streams.size(); ++i) {
                        Stream& st = pr.streams[i];
                        st.extra.insert(qFromUtf8("source"), qFromUtf8("web"));
                        st.extra.insert(qFromUtf8("requestedQuality"), quality);
                        if (quality::heightOf(st.quality) != wantH)
                            st.extra.insert(qFromUtf8("degraded"), true);
                    }
                    pr.item.extra.insert(qFromUtf8("seriesId"), seriesId);
                }
                cb(r, pr);
            });
        };
        if (!vid.isEmpty()) { viaWeb(vid); return; }
        detail(seriesId, [cb, viaWeb](Result res, const VideoItem&,
                                      const QList<VideoItem>& parts) {
            if (res.code != Err::Ok || parts.isEmpty()) { cb(res, {}); return; }
            viaWeb(parts.first().id);
        });
        return;
    }

    // 通道②:App 协议(零签名 H3a)+ 本地流式解密代理(H2b)。
    // 只有 bytevc1(HEVC 通用可播)才走;bytevc2 私有编码 → web 保底并留说明。
    auto tryApp = [this, cb, seriesId, playInfo, quality, ckey](
                      const QString& vid2) {
        hgapp::videoModel(vid2, [this, cb, seriesId, vid2, playInfo, quality,
                                 ckey](
                                    const Result& r,
                                    const QList<hgapp::Track>& tracks) {
            if (r.code != Err::Ok) {
                DBG_P(kMod, Info, "app chain failed (%s) -> web fallback",
                      qPrintable(r.message));
                playInfo(vid2, cb);
                return;
            }
            // 指定清晰度:按 definition 精确选(大小写不敏感);
            // 未指定 → pickBest(优先 bytevc1 最高档)
            const hgapp::Track* best = nullptr;
            if (quality.trimmed().isEmpty()) {
                best = hgapp::pickBest(tracks);
            } else {
                for (int i = 0; i < tracks.size(); ++i)
                    if (QXV_IEQ(tracks[i].definition, quality)) {
                        best = &tracks[i];
                        break;
                    }
            }
            if (!best) {
                QStringList have;
                for (int i = 0; i < tracks.size(); ++i)
                    have << tracks[i].definition;
                cb(Result::fail(Err::NotFound,
                   qFromUtf8("hongguo: 无 '%1' 档(可选 %2)")
                       .arg(quality, have.join(qFromUtf8("/")))), {});
                return;
            }
            if (!best->playable()) {
                // bytevc2(<=720p,含 160/240/360/480/540/720)是字节私有编码,
                // 标准播放器解不了;不静默降级,明确告知只有 1080p 可播。
                cb(Result::fail(Err::Unsupported,
                   qFromUtf8("hongguo: %1 为 bytevc2 私有编码不可播"
                             "(≤720p 均如此),请选 1080p(bytevc1)")
                       .arg(best->definition)), {});
                return;
            }
            QString localUrl;
            const Result o = hgstream::Server::instance().open(
                vid2, best->definition, best->url, best->spadeA, &localUrl);
            if (o.code != Err::Ok) {
                DBG_P(kMod, Warn, "local proxy failed (%s) -> web fallback",
                      qPrintable(o.message));
                playInfo(vid2, cb);
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
            if (!quality.trimmed().isEmpty())
                st.extra.insert(qFromUtf8("requestedQuality"), quality);
            pr.streams.append(st);
            DBG_P(kMod, Info, "resolve %s -> app %s (local proxy %s)",
                  qPrintable(vid2), qPrintable(best->definition),
                  qPrintable(localUrl));
            // 本地代理地址 10 分钟内复用(会话 refs 在,地址有效)
            m_cache.put(ckey, pr, 10 * 60 * 1000);
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
