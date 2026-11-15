#include "compat.h"
#include "bilibiliprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/qcjson.h"

#include <QUrlQuery>
#include <functional>

// bilibili web REST(游客口径,bilibili-API-collect):
//   搜索  GET https://api.bilibili.com/x/web-interface/search/all/v2?keyword=
//   视频信息 GET https://api.bilibili.com/x/web-interface/view?bvid=
//   播放地址 GET https://api.bilibili.com/x/player/playurl?bvid=&cid=&qn=64&platform=html5
//     → html5 平台游客直出 mp4(durl,单段);高清 dash 需登录 → NeedAuth
// 风控:无 UA/Referer → -412;搜索建议先 GET 首页种 buvid3 cookie
// 解析:cJSON(qlcomp 同源份,Qt3 兼容);禁 QJson

namespace qxv {

static const char* kMod = "bilibili";

// 校验 REST 信封:code!=0 → 风控映射;root 在 root 输出(调用方持 JRoot)
static Result checkApi(const QByteArray& body, JRoot& root) {
    root.parse(body);
    if (!root.ok())
        return Result::fail(Err::Parse, qFromUtf8("bilibili: body not json"));
    const int code = static_cast<int>(jint(root.p, "code", -1));
    if (code != 0) {
        // -412 风控 / -101 未登录 / -404 不存在
        Err e = code == -412 ? Err::RateLimit
              : code == -101 ? Err::NeedAuth
              : code == -404 ? Err::NotFound : Err::Parse;
        return Result::fail(e,
            qFromUtf8("bilibili code=%1 %2")
                .arg(code).arg(jstr(root.p, "message")));
    }
    return Result::ok();
}

static QList<VideoItem> parseSearch(cJSON* results) {
    QList<VideoItem> out;
    if (!results) return out;
    for (int i = 0; i < jsize(results); ++i) {
        const cJSON* g = jat(results, i);          // result_type 分组
        if (jstr(g, "result_type") != qFromUtf8("video")) continue;
        cJSON* data = jarr(g, "data");
        for (int k = 0; k < jsize(data); ++k) {
            const cJSON* it = jat(data, k);
            VideoItem vi;
            vi.id     = jstr(it, "bvid");
            vi.title  = jstr(it, "title");
            vi.author = jstr(it, "author");
            vi.uid    = QString::number(jint(it, "mid"));
            vi.cover  = jstr(it, "pic");
            vi.durationMs = jint(it, "duration") * 1000;
            out.append(vi);
        }
    }
    return out;
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
        net.send(warm, [](const NetResponse&) {});
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
    net.send(req, std::move(cb));
}

void BilibiliProvider::search(const QString& kw, int page, SearchCb cb) {
    QUrlQuery q;
    q.addQueryItem("keyword", kw);
    q.addQueryItem("page", QString::number(page));
    apiGet("/x/web-interface/search/all/v2", q.toString(QUrl::FullyEncoded),
           [cb, page](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        Result res;
        JRoot root;
        res = checkApi(r.body, root);
        if (res.code != Err::Ok) { cb(res, {}, false); return; }
        const QList<VideoItem> items = parseSearch(
            jarr(jobj(root.p, "data"), "result"));
        cb(Result::ok(), items, !items.isEmpty());   // 分页 hasMore 粗判
    });
}

void BilibiliProvider::detail(const QString& id, DetailCb cb) {
    QUrlQuery q;
    q.addQueryItem("bvid", id);
    apiGet("/x/web-interface/view", q.toString(QUrl::FullyEncoded),
           [cb, id](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, {}); return; }
        JRoot root;
        const Result res = checkApi(r.body, root);
        if (res.code != Err::Ok) { cb(res, {}, {}); return; }
        const cJSON* data = jobj(root.p, "data");
        VideoItem vi;
        vi.id     = jstr(data, "bvid");
        vi.title  = jstr(data, "title");
        vi.author = jstr(jobj(data, "owner"), "name");
        vi.cover  = jstr(data, "pic");
        vi.durationMs = jint(data, "duration") * 1000;
        QList<VideoItem> parts;
        cJSON* pages = jarr(data, "pages");
        for (int i = 0; i < jsize(pages); ++i) {
            const cJSON* p = jat(pages, i);
            VideoItem part;
            part.id = QString::number(jint(p, "cid"));
            part.title = jstr(p, "part");
            part.durationMs = jint(p, "duration") * 1000;
            part.extra = QVariantMap{ { "bvid", vi.id } };
            parts.append(part);
        }
        if (parts.isEmpty())   // 单 P:直接用主项
            parts.append(vi);
        cb(Result::ok(), vi, parts);
    });
}

void BilibiliProvider::resolve(const QString& id, ResolveCb cb) {
    // id 形如 "BV1xx411c7mD@cid123"(detail 出的分集)或纯 bvid(单P)
    QString bvid = id, cid;
    const int at = id.indexOf(qFromUtf8("."));
    if (at > 0) { bvid = id.left(at); cid = id.mid(at + 1); }

    auto playUrl = [this, cb](const QString& bvid2, const QString& cid2) {
        QUrlQuery q;
        q.addQueryItem("bvid", bvid2);
        q.addQueryItem("cid", cid2);
        q.addQueryItem("qn", "64");          // 高清 720P(html5 上限)
        q.addQueryItem("platform", "html5");
        apiGet("/x/player/playurl", q.toString(QUrl::FullyEncoded),
               [cb, bvid2](const NetResponse& r) {
            if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}); return; }
            JRoot root;
            const Result res = checkApi(r.body, root);
            if (res.code != Err::Ok) { cb(res, {}); return; }
            PlayResult pr;
            cJSON* durl = jarr(jobj(root.p, "data"), "durl");
            for (int i = 0; i < jsize(durl); ++i) {
                const cJSON* dv = jat(durl, i);
                Stream s;
                s.url = jstr(dv, "url");
                s.quality = qFromUtf8("720P");
                s.kind = qFromUtf8("mp4");
                s.extra = QVariantMap{
                    { "Referer", qFromUtf8("https://www.bilibili.com/") } };
                pr.streams.append(s);
            }
            if (pr.streams.isEmpty())
                cb(Result::fail(Err::NeedAuth,
                   qFromUtf8("bilibili: no guest streams (dash only)")), {});
            else
                cb(Result::ok(), pr);
        });
    };

    if (cid.isEmpty()) {
        detail(bvid, [cb, playUrl](Result res, const VideoItem&,
                                   const QList<VideoItem>& parts) {
            if (res.code != Err::Ok || parts.isEmpty()) {
                cb(res, {}); return;
            }
            playUrl(parts.first().extra.value("bvid").toString(),
                    parts.first().id);
        });
        return;
    }
    playUrl(bvid, cid);
}

} // namespace qxv
