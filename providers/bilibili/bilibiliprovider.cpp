#include "compat.h"
#include "bilibiliprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/qcjson.h"
#include "parse.h"
#include "common/quality.h"
#include "common/autherror.h"

#include <QUrlQuery>
#include <functional>
#include <ctime>

// bilibili web REST(游客口径,bilibili-API-collect):
//   搜索  GET https://api.bilibili.com/x/web-interface/search/all/v2?keyword=
//   视频信息 GET https://api.bilibili.com/x/web-interface/view?bvid=
//   播放地址 GET https://api.bilibili.com/x/player/playurl?bvid=&cid=&qn=64&platform=html5
//     → html5 平台游客直出 mp4(durl,单段);高清 dash 需登录 → NeedAuth
// 风控:无 UA/Referer → -412;搜索建议先 GET 首页种 buvid3 cookie
// 解析:cJSON(qlcomp 同源份,Qt3 兼容);禁 QJson

namespace qxv {

void BilibiliProvider::cancel() {
    m_net.cancel();      // 在途/重试/待投递回调全部作废
    m_cache.clear();     // 直链缓存一并清(避免播到已取消请求对应的旧地址)
}


static const char* kMod = "bilibili";

// 校验 REST 信封:code!=0 → 风控映射;root 在 root 输出(调用方持 JRoot)
// ctx:凭证状态(用于区分"游客需登录"与"凭证失效")
static Result checkApi(const QByteArray& body, JRoot& root,
                       const AuthContext& ctx = AuthContext()) {
    root.parse(body);
    if (!root.ok())
        return Result::fail(Err::Parse, qFromUtf8("bilibili: body not json"));
    const int code = static_cast<int>(jint(root.p, "code", -1));
    if (code != 0) {
        const QString msg = jstr(root.p, "message");
        const QString codeStr = QString::number(code);
        // -412 风控 / -101 未登录 / -404 不存在
        if (code == -101) {
            AuthContext c = ctx;
            c.expired = ctx.provided;   // 有凭证仍 -101 = 失效
            return authFail(c.provided ? Err::AuthExpired : Err::NeedAuth, c,
                            qFromUtf8("bilibili code=%1 %2").arg(code).arg(msg),
                            200, codeStr, msg);
        }
        Err e = code == -412 ? Err::RateLimit
              : code == -404 ? Err::NotFound : Err::Parse;
        return withServer(
            Result::fail(e, qFromUtf8("bilibili code=%1 %2").arg(code).arg(msg)),
            200, codeStr, msg);
    }
    return Result::ok();
}


void BilibiliProvider::apiGet(const QString& path, const QString& query,
                              std::function<void(const NetResponse&)> cb) {
    static NetClient net;           // 进程级共享(httplib 阻塞型,线程池在内部)
    static CookieJar jar;
    if (jar.cookieForUrl(qFromUtf8("https://www.bilibili.com")).isEmpty()) {
        // 首次:种 buvid3(风控要求)
        NetRequest warm;
        warm.url = qFromUtf8("https://www.bilibili.com/");
        for (const auto& h : Fingerprint::chromeWeb(
                 qFromUtf8("zh-CN,en-US")).headers)
            warm.headers.append(h);
        warm.headers.append(qMakePair(qFromUtf8("Referer"),
            qFromUtf8("https://www.bilibili.com/")));
        warm.maxAttempts = 2;
        m_net.send(warm, [](const NetResponse&) {});
    }

    NetRequest req;
    req.url = qFromUtf8("https://api.bilibili.com%1?%2").arg(path, query);
    for (const auto& h : Fingerprint::chromeWeb(
             qFromUtf8("zh-CN,en-US")).headers)
        req.headers.append(h);
    req.headers.append(qMakePair(qFromUtf8("Referer"),
        qFromUtf8("https://www.bilibili.com/")));
    const QString token = m_cred.value("cookie").toString();
    if (!token.isEmpty())
        req.headers.append({ "Cookie", token });
    DBG_P(kMod, Debug, "GET %s", qPrintable(req.url));
    m_net.send(req, std::move(cb));
}

void BilibiliProvider::search(const QString& kw, int page, SearchCb cb) {
    QUrlQuery q;
    q.addQueryItem("keyword", kw);
    q.addQueryItem("page", QString::number(page));
    q.addQueryItem("page_size", "20");   // 实测:pagesize=20,numPages=50
    apiGet("/x/web-interface/search/all/v2", q.toString(QUrl::FullyEncoded),
           [this, cb, page, kw](const NetResponse& r) {
        if (!r.ok()) {
            cb(withServer(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error),
                   r.status, QString(), QString::fromUtf8(r.body.left(200))),
               {}, false);
            return;
        }
        Result res;
        JRoot root;
        res = checkApi(r.body, root, credContext(m_cred));
        if (res.code != Err::Ok) { cb(res, {}, false); return; }
        const cJSON* data = jobj(root.p, "data");
        const QList<VideoItem> items = bilibili::parse::searchResult(root.p);
        // 服务端给总页数(实测 numPages=50);缺失时退化为"本页满 20 条即还有"
        const long long numPages = jint(data, "numPages", 0);
        const bool hasMore = numPages > 0 ? (page < numPages)
                                          : items.size() >= 20;
        DBG_P(kMod, Debug, "search '%s' p%d -> %d items (numPages=%lld)",
              qPrintable(kw), page, items.size(), numPages);
        cb(Result::ok(), items, hasMore);
    });
}

// ── 分类:popular 条目自带 tid/tname(动态,无硬编码表) ─────────────
// 缓存 30min(分区变化极慢;失败不缓存,下次重试)
static QList<CategoryItem> g_regions;
static long long g_regionsAt = 0;
static const int kRegionsTtlSec = 30 * 60;

void BilibiliProvider::categories(
    std::function<void(const Result&, const QList<CategoryItem>&)> cb) {
    const long long now = (long long)::time(0);
    if (!g_regions.isEmpty() && now - g_regionsAt < kRegionsTtlSec) {
        DBG_P(kMod, Debug, "categories: cache hit (%d)", g_regions.size());
        cb(Result::ok(), g_regions);
        return;
    }
    QUrlQuery q;
    q.addQueryItem("pn", "1");
    q.addQueryItem("ps", "20");
    DBG_P(kMod, Info, "categories: GET popular (derive regions)");
    apiGet("/x/web-interface/popular", q.toString(QUrl::FullyEncoded),
           [this, cb](const NetResponse& r) {
        if (!r.ok()) {
            DBG_P(kMod, Warn, "categories: popular http=%d err=%s", r.status,
                  qPrintable(r.error));
            cb(withServer(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error),
                   r.status, QString(), QString::fromUtf8(r.body.left(200))), {});
            return;
        }
        JRoot root;
        const Result res = checkApi(r.body, root, credContext(m_cred));
        if (res.code != Err::Ok) {
            DBG_P(kMod, Warn, "categories: checkApi code=%d %s",
                  (int)res.code, qPrintable(res.message));
            cb(res, {});
            return;
        }
        QList<CategoryItem> regs = bilibili::parse::popularRegions(root.p);
        DBG_P(kMod, Info, "categories: %d regions from popular", regs.size());
        if (regs.isEmpty()) {
            DBG_P(kMod, Warn, "categories: 0 regions (popular 结构可能变更)");
            cb(Result::fail(Err::Parse, qFromUtf8("bilibili: 分区表为空")), {});
            return;
        }
        regs.prepend(CategoryItem(QString(), QString()));   // 推荐
        g_regions = regs;
        g_regionsAt = (long long)::time(0);
        cb(Result::ok(), regs);
    });
}

void BilibiliProvider::categoryFeed(const QString& key, int page, SearchCb cb) {
    if (key.isEmpty()) {   // 推荐
        DBG_P(kMod, Debug, "categoryFeed: 推荐 -> homeFeed p%d", page);
        homeFeed(page, cb);
        return;
    }
    if (page < 1) page = 1;
    /* key = "<tid>|<tname>"(categories() 产出,UI 不解释)。
     * 二级分区 tid 在 newlist 常为空 → 用分区名走站内搜索兜底(可翻页)。 */
    QString rid = key, tname;
    const int bar = key.indexOf(qFromUtf8("|"));
    if (bar >= 0) { rid = key.left(bar); tname = key.mid(bar + 1); }
    const bool hasRid = rid.toLongLong() > 0;

    if (!hasRid) {
        if (tname.isEmpty()) {
            DBG_P(kMod, Warn, "categoryFeed: 非法 key='%s'", qPrintable(key));
            cb(Result::fail(Err::Unsupported,
               qFromUtf8("bilibili: 分类 key 无效")), {}, false);
            return;
        }
        DBG_P(kMod, Info, "categoryFeed: '%s' 无 rid → 搜索兜底 p%d",
              qPrintable(tname), page);
        search(tname, page, cb);
        return;
    }

    QUrlQuery q;
    q.addQueryItem("rid", rid);
    q.addQueryItem("pn", QString::number(page));
    q.addQueryItem("ps", "20");
    DBG_P(kMod, Info, "categoryFeed: rid=%s p%d", qPrintable(rid), page);
    apiGet("/x/web-interface/newlist", q.toString(QUrl::FullyEncoded),
           [this, cb, rid, tname, key, page](const NetResponse& r) {
        if (!r.ok()) {
            DBG_P(kMod, Warn, "categoryFeed rid=%s p%d http=%d err=%s",
                  qPrintable(rid), page, r.status, qPrintable(r.error));
            cb(withServer(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error),
                   r.status, QString(), QString::fromUtf8(r.body.left(200))),
               {}, false);
            return;
        }
        JRoot root;
        const Result res = checkApi(r.body, root, credContext(m_cred));
        if (res.code != Err::Ok) {
            DBG_P(kMod, Warn, "categoryFeed rid=%s p%d code=%d",
                  qPrintable(rid), page, (int)res.code);
            cb(res, {}, false);
            return;
        }
        bool hasMore = false;
        const QList<VideoItem> items =
            bilibili::parse::newlist(root.p, page, &hasMore);
        DBG_P(kMod, Debug, "categoryFeed rid=%s p%d -> %d items (hasMore=%d)",
              qPrintable(rid), page, items.size(), int(hasMore));
        if (items.isEmpty() && !tname.isEmpty()) {
            DBG_P(kMod, Warn,
                  "categoryFeed rid=%s p%d empty → '%s' 搜索兜底(key=%s)",
                  qPrintable(rid), page, qPrintable(tname), qPrintable(key));
            search(tname, page, cb);
            return;
        }
        if (items.isEmpty())
            DBG_P(kMod, Warn, "categoryFeed rid=%s p%d empty (无内容/rid 失效)",
                  qPrintable(rid), page);
        cb(Result::ok(), items, hasMore);
    });
}

// ── homeFeed(热门榜,游客口)─────────────────────────────────────────
// GET /x/web-interface/popular?pn=&ps=20 → data.list[] + data.no_more
void BilibiliProvider::homeFeed(int page, SearchCb cb) {
    if (page < 1) page = 1;
    QUrlQuery q;
    q.addQueryItem("pn", QString::number(page));
    q.addQueryItem("ps", "20");
    apiGet("/x/web-interface/popular", q.toString(QUrl::FullyEncoded),
           [this, cb, page](const NetResponse& r) {
        if (!r.ok()) {
            cb(withServer(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error),
                   r.status, QString(), QString::fromUtf8(r.body.left(200))),
               {}, false);
            return;
        }
        JRoot root;
        const Result res = checkApi(r.body, root, credContext(m_cred));
        if (res.code != Err::Ok) { cb(res, {}, false); return; }
        bool hasMore = false;
        const QList<VideoItem> items = bilibili::parse::popular(root.p, &hasMore);
        DBG_P(kMod, Debug, "homeFeed p%d -> %d items (hasMore=%d)",
              page, items.size(), int(hasMore));
        cb(Result::ok(), items, hasMore);
    });
}

void BilibiliProvider::detail(const QString& id, DetailCb cb) {
    QUrlQuery q;
    q.addQueryItem("bvid", id);
    apiGet("/x/web-interface/view", q.toString(QUrl::FullyEncoded),
           [this, cb, id](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, {}); return; }
        JRoot root;
        const Result res = checkApi(r.body, root, credContext(m_cred));
        if (res.code != Err::Ok) { cb(res, {}, {}); return; }
        VideoItem vi;
        QList<VideoItem> parts;
        const Result dres = bilibili::parse::detailFromView(root.p, &vi, &parts);
        if (dres.code != Err::Ok) { cb(dres, {}, {}); return; }
        if (!parts.isEmpty()) { cb(Result::ok(), vi, parts); return; }

        // pages 缺失(部分老稿件/接口裁剪)→ 用 pagelist 兜底
        QUrlQuery q2;
        q2.addQueryItem("bvid", vi.id);
        apiGet("/x/player/pagelist", q2.toString(QUrl::FullyEncoded),
               [this, cb, vi](const NetResponse& r2) {
            if (!r2.ok()) {
                cb(withServer(Result::fail(Err::Network, r2.error), r2.status,
                              QString(), QString::fromUtf8(r2.body.left(200))),
                   vi, {});
                return;
            }
            JRoot root2;
            const Result res2 = checkApi(r2.body, root2, credContext(m_cred));
            QList<VideoItem> parts2;
            if (res2.code == Err::Ok)
                parts2 = bilibili::parse::pageList(root2.p, vi.id);
            if (parts2.isEmpty()) parts2.append(vi);   // 单 P:主项即唯一分集
            cb(Result::ok(), vi, parts2);
        });
    });
}

// 清晰度 → qn(html5 平台实测只支持 16=360P / 64=720P:
// 请求 32 会回 16,请求 80/112 会回 64)。
// 160/240/480 等低档 html5 无对应 qn → 就近取 16(360P)并标 degraded。
static int qnForQuality(const QString& quality, bool* recognized) {
    *recognized = true;
    const int h = quality::heightOf(quality);
    if (h == 0) return 64;          // 空/auto/best
    if (h < 0) { *recognized = false; return 64; }
    if (h <= 360) return 16;        // 160/240/360 → 360(html5 最低)
    if (h <= 540) return 32;        // 480/540 → 服务端回 360
    if (h <= 720) return 64;
    return 80;                      // >720 → 服务端回 720(html5 上限)
}

// 服务端实际档位(quality 字段)→ 展示口径
static QString labelForQn(long long qn) {
    return qn >= 64 ? qFromUtf8("720P") : qFromUtf8("360P");
}

void BilibiliProvider::resolve(const QString& id, ResolveCb cb) {
    resolveQuality(id, QString(), std::move(cb));
}

void BilibiliProvider::resolveQuality(const QString& id, const QString& quality,
                                      ResolveCb cb) {
    // id 形如 "BV1xx411c7mD@cid123"(detail 出的分集)或纯 bvid(单P)
    QString bvid = id, cid;
    const int at = id.indexOf(qFromUtf8("."));
    if (at > 0) { bvid = id.left(at); cid = id.mid(at + 1); }

    bool recognized = true;
    const int qn = qnForQuality(quality, &recognized);
    if (!recognized) {
        cb(Result::fail(Err::Unsupported,
           qFromUtf8("bilibili: 未知清晰度 '%1'(支持 360p/480p/720p)").arg(quality)),
           {});
        return;
    }

    // 直链缓存:CDN URL 约 2h 过期;命中直接返回,过期自动重解析
    const QString ckey = qFromUtf8("bilibili|%1|%2").arg(id, quality);
    {
        PlayResult cached;
        if (m_cache.get(ckey, &cached)) {
            DBG_P(kMod, Debug, "resolve cache hit %s", qPrintable(id));
            cb(Result::ok(), cached);
            return;
        }
    }

    auto playUrl = [this, cb, quality, qn, ckey](const QString& bvid2,
                                           const QString& cid2) {
        QUrlQuery q;
        q.addQueryItem("bvid", bvid2);
        q.addQueryItem("cid", cid2);
        q.addQueryItem("qn", QString::number(qn));
        q.addQueryItem("platform", "html5");
        apiGet("/x/player/playurl", q.toString(QUrl::FullyEncoded),
               [this, cb, bvid2, quality, ckey](const NetResponse& r) {
            if (!r.ok()) {
                cb(withServer(Result::fail(Err::Network, r.error), r.status,
                              QString(), QString::fromUtf8(r.body.left(200))), {});
                return;
            }
            JRoot root;
            const Result res = checkApi(r.body, root, credContext(m_cred));
            if (res.code != Err::Ok) { cb(res, {}); return; }
            const cJSON* data = jobj(root.p, "data");
            PlayResult pr;
            cJSON* durl = jarr(data, "durl");
            if (durl && jsize(durl) > 0) {
                const QString actual = labelForQn(jint(data, "quality", 0));
                for (int i = 0; i < jsize(durl); ++i) {
                    const cJSON* dv = jat(durl, i);
                    Stream s;
                    s.url = jstr(dv, "url");
                    s.quality = actual;
                    s.kind = qFromUtf8("mp4");
                    s.extra = QVariantMap{
                        { "Referer", qFromUtf8("https://www.bilibili.com/") } };
                    if (!quality.isEmpty()) {
                        s.extra.insert(qFromUtf8("requestedQuality"), quality);
                        // 请求档与服务端实际档不一致(含"给高了")→ 标降级
                        const int want = quality::heightOf(quality);
                        const int got = quality::heightOf(actual);
                        if (want > 0 && got > 0 && want != got) {
                            s.extra.insert(qFromUtf8("degraded"), true);
                            s.extra.insert(qFromUtf8("actualQuality"), actual);
                        }
                    }
                    pr.streams.append(s);
                }
            }
            if (pr.streams.isEmpty())
                cb(Result::fail(Err::NeedAuth,
                   qFromUtf8("bilibili: no guest streams (dash only)")), {});
            else {
                m_cache.put(ckey, pr, streamcache::ttlFromUrl(
                    pr.streams.first().url, 90 * 60 * 1000));
                cb(Result::ok(), pr);
            }
        });
    };

    if (cid.isEmpty()) {
        detail(bvid, [cb, playUrl](Result res, const VideoItem&,
                                   const QList<VideoItem>& parts) {
            if (res.code != Err::Ok || parts.isEmpty()) {
                cb(res, {}); return;
            }
            // 分P 的 part.id 是 "bvid.cid" 复合形式,这里必须取 extra.cid
            QString cid = parts.first().extra.value(qFromUtf8("cid")).toString();
            if (cid.isEmpty()) cid = parts.first().id;
            playUrl(parts.first().extra.value(qFromUtf8("bvid")).toString(),
                    cid);
        });
        return;
    }
    playUrl(bvid, cid);
}
} // namespace qxv
