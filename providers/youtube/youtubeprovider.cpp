#include "compat.h"
#include "youtubeprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/qcjson.h"

#include <QUrlQuery>
#include <functional>

// InnerTube(游客口径,yt-dlp/pytubefix 同款):
//   POST https://www.youtube.com/youtubei/v1/player?prettyPrint=false
//   POST https://www.youtube.com/youtubei/v1/search?prettyPrint=false
// client 档位:ANDROID_VR 优先(免 key/免 PoToken/免 decipher,直出签名 url)
//             → IOS 备选;失败回 WEB
// UA 必须与 clientVersion 匹配:"com.google.android.apps.youtube.vr.oculus/..."
// 风控:playabilityStatus.status != OK → LOGIN_REQUIRED(年龄)/UNPLAYABLE(地区)
// 解析:cJSON(qlcomp 同源份);禁 QJson(Qt3 无此 API)
// client 档位:ANDROID_VR 优先(免 key/免 PoToken/免 decipher,直出签名 url)
//             → IOS 备选;失败回 WEB
// UA 必须与 clientVersion 匹配:"com.google.android.apps.youtube.vr.oculus/..."
// 风控:playabilityStatus.status != OK → LOGIN_REQUIRED(年龄)/UNPLAYABLE(地区)
// ★ IP 级 bot 风控(参考资料,按优先级):
//   - iv-org/invidious:https://github.com/iv-org/invidious
//     issue#4734(datacenter IP 全员命中)+ PR#4789(WEB client+po_token+visitorData)
//     + 官方方案文档:https://docs.invidious.io/youtube-errors-explained/
//     + invidious-companion(新签名栈)/inv_sig_helper(decipher)
//   - iv-org/youtube-trusted-session-generator:生成 po_token/visitor_data
//   - yt-dlp + BGutil POT provider(BGutil-youtube-pot-provider):
//     ANDROID_VR+PO token 可解 datacenter IP 的 LOGIN_REQUIRED(ytagent-cli 实测)
//   - AlvaroBalbin/yt-innertube:ANDROID_VR 免 decipher 直出 url 的实现参考
//   本实现:先裸 ANDROID_VR(住宅 IP 常通);命中风控 → CaptchaRequired 传导 UI

namespace qxv {

static const char* kMod = "youtube";

struct YtClient {
    const char* name;      // clientName
    const char* version;   // clientVersion
    const char* ua;
};
// ANDROID_VR 校验最松(2026 实测 OK,免 key/PoToken);IOS 备选(需字段对齐版本)
static const YtClient kClients[] = {
    { "ANDROID_VR", "1.60.19",
      "com.google.android.apps.youtube.vr.oculus/1.60.19 (Linux; U; Android 12; "
      "eureka-user Build/SQ3A.220605.009.A1) gzip" },
    { "IOS", "19.45.4",
      "com.google.ios.youtube/19.45.4 (iPhone16,2; U; CPU iOS 18_1_1 like Mac OS X)" },
};

void YoutubeProvider::innertube(const QString& endpoint, const QByteArray& body,
                                std::function<void(const NetResponse&)> cb) {
    static NetClient net;
    static CookieJar jar;
    static bool jarInit = (net.setCookieJar(&jar), true);
    static bool warmed = false;
    const YtClient& c = kClients[0];
    // player 前必须预热:先 GET 首页种 cookie(YOS/SOCS 等),
    // 否则 playabilityStatus=LOGIN_REQUIRED "confirm you're not a bot"
    if (!warmed) {
        warmed = true;
        NetRequest pre;
        pre.url = QXV_STR("https://www.youtube.com/");
        // 必须带浏览器 UA:裸 UA(httplib 默认)会拿到风控页并种毒 cookie
        pre.headers = Fingerprint::chromeWeb(QXV_STR("en-US,en")).headers;
        pre.autoCookie = true;
        pre.proxy = m_proxy.enabled
            ? QXV_STR("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port)
            : QXV_ENV("QXVOTUBE_PROXY");
        net.send(pre, [this, endpoint, body, cb](const NetResponse&) {
            innertube(endpoint, body, cb);
        });
        return;
    }
    NetRequest req;
    req.url = QXV_STR(
        "https://www.youtube.com/youtubei/v1/%1?prettyPrint=false").arg(endpoint);
    req.method = QXV_STR("POST");
    req.body = body;
    req.headers = {
        { "Content-Type", "application/json" },
        { "User-Agent", QString::fromUtf8(c.ua) },
        { "Origin", "https://www.youtube.com" },
    };
    req.autoCookie = true;
    // 代理来源:setProxy() 显式设置,或 QXVOTUBE_PROXY=http://host:port(YouTube 被墙直连必超时)
    if (m_proxy.enabled) {
        req.proxy = QXV_STR("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    } else {
        req.proxy = QXV_ENV("QXVOTUBE_PROXY");
    }
    DBG_P(kMod, Debug, "POST /%s (%d bytes)", qPrintable(endpoint), body.size());
    net.send(req, std::move(cb));
}

// cJSON 构造 body(自动转义 kw 等),打印后即弃
static QByteArray dumpBody(cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    const QByteArray b = QByteArray(s);
    cJSON_free(s);
    cJSON_Delete(root);
    return b;
}

static QByteArray playerBody(const QString& videoId) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    cJSON* cli = cJSON_AddObjectToObject(ctx, "client");
    cJSON_AddStringToObject(cli, "clientName", "ANDROID_VR");
    cJSON_AddStringToObject(cli, "clientVersion", "1.60.19");
    cJSON_AddStringToObject(cli, "deviceMake", "Oculus");
    cJSON_AddStringToObject(cli, "deviceModel", "Quest 3");
    cJSON_AddStringToObject(cli, "osName", "Android");
    cJSON_AddStringToObject(cli, "osVersion", "12");
    cJSON_AddStringToObject(cli, "hl", "en");
    cJSON_AddStringToObject(cli, "gl", "US");
    cJSON_AddStringToObject(root, "videoId", videoId.toUtf8().constData());
    cJSON_AddBoolToObject(root, "contentCheckOk", 1);
    cJSON_AddBoolToObject(root, "racyCheckOk", 1);
    return dumpBody(root);
}

static QByteArray searchBody(const QString& kw) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    cJSON* cli = cJSON_AddObjectToObject(ctx, "client");
    cJSON_AddStringToObject(cli, "clientName", "WEB");
    cJSON_AddStringToObject(cli, "clientVersion", "2.20260101.00.00");
    cJSON_AddStringToObject(cli, "hl", "en");
    cJSON_AddStringToObject(root, "query", kw.toUtf8().constData());
    return dumpBody(root);
}

// 解析 playabilityStatus:非 OK 转 Result
static Result playability(const cJSON* root) {
    const cJSON* ps = jobj(root, "playabilityStatus");
    const QString st = jstr(ps, "status");
    if (st == QXV_STR("OK")) return Result::ok();
    const QString reason = jstr(ps, "reason");
    const QString low = reason.toLower();
    Err e = st.contains(QXV_STR("LOGIN")) ? Err::NeedAuth
          : st.contains(QXV_STR("UNPLAYABLE")) ? Err::GeoBlocked
          : Err::NotFound;
    // 真人识别传导:bot 检查(登录可解)→ NeedAuth;
    // unusual traffic/captcha(需人机验证)→ CaptchaRequired,detail.captchaUrl
    // 供 UI 弹对话框打开 challenge(§docs/protocols/youtube.md)
    if (low.contains(QXV_STR("captcha")) ||
        low.contains(QXV_STR("unusual traffic")) ||
        low.contains(QXV_STR("not a bot"))) {
        e = Err::CaptchaRequired;
        Result r = Result::fail(e, QXV_STR("youtube playability %1: %2")
                                    .arg(st, reason));
        r.detail.insert(QXV_STR("captchaUrl"), QXV_STR("https://www.youtube.com/"));
        return r;
    }
    return Result::fail(e, QXV_STR("youtube playability %1: %2")
                            .arg(st, reason));
}

void YoutubeProvider::search(const QString& kw, int page, SearchCb cb) {
    Q_UNUSED(page);   // P1:首页 20 条;分页用 continuationToken(P2)
    innertube("search", searchBody(kw), [cb, kw](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, false); return; }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        QList<VideoItem> items;
        // 通用递归遍历(结构随版本改版,不写死路径):
        // 任意层级的 videoRenderer 都收
        std::function<void(cJSON*)> walk = [&](cJSON* n) {
            if (!n) return;
            if (cJSON_IsObject(n)) {
                cJSON* vr = cJSON_GetObjectItemCaseSensitive(n, "videoRenderer");
                if (cJSON_IsObject(vr)) {
                    VideoItem vi;
                    vi.id = jstr(vr, "videoId");
                    cJSON* runs = jarr(jobj(vr, "title"), "runs");
                    vi.title = jstr(jat(runs, 0), "text");
                    runs = jarr(jobj(vr, "ownerText"), "runs");
                    vi.author = jstr(jat(runs, 0), "text");
                    cJSON* thumbs = jarr(jobj(vr, "thumbnail"), "thumbnails");
                    vi.cover = jstr(jat(thumbs, jsize(thumbs) - 1), "url");
                    if (!vi.id.isEmpty()) items.append(vi);
                }
                cJSON* ch;
                cJSON_ArrayForEach(ch, n) walk(ch);
            } else if (cJSON_IsArray(n)) {
                cJSON* ch;
                cJSON_ArrayForEach(ch, n) walk(ch);
            }
        };
        walk(root.p);
        DBG_P(kMod, Debug, "search '%s' -> %d items", qPrintable(kw), items.size());
        if (items.isEmpty())
            DBG_P(kMod, Trace, "search empty body head: %.400s", r.body.constData());
        cb(Result::ok(), items, false);
    });
}

void YoutubeProvider::detail(const QString& id, DetailCb cb) {
    innertube("player", playerBody(id), [cb, id](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, {}); return; }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, {}); return; }
        Result res = playability(root.p);
        if (res.code != Err::Ok) { cb(res, {}, {}); return; }
        const cJSON* vd = jobj(root.p, "videoDetails");
        VideoItem vi;
        vi.id = jstr(vd, "videoId");
        if (vi.id.isEmpty()) vi.id = id;
        vi.title = jstr(vd, "title");
        vi.author = jstr(vd, "author");
        vi.uid = jstr(vd, "channelId");
        vi.durationMs = jint(vd, "lengthSeconds") * 1000;
        cb(Result::ok(), vi, QList<VideoItem>{ vi });   // 单视频
    });
}

void YoutubeProvider::resolve(const QString& id, ResolveCb cb) {
    innertube("player", playerBody(id), [cb, id](const NetResponse& r) {
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}); return; }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}); return; }
        Result res = playability(root.p);
        if (res.code != Err::Ok) { cb(res, {}); return; }
        PlayResult pr;
        pr.item.id = id;
        cJSON* formats = jarr(jobj(root.p, "streamingData"), "formats");
        for (int i = 0; i < jsize(formats); ++i) {
            const cJSON* f = jat(formats, i);
            Stream s;
            s.url = jstr(f, "url");
            s.quality = QXV_STR("%1x%2")
                .arg(static_cast<int>(jint(f, "width")))
                .arg(static_cast<int>(jint(f, "height")));
            s.kind = QXV_STR("mp4");
            if (!s.url.isEmpty()) pr.streams.append(s);
        }
        // 混合流(自适应 DASH)仅取最高一档 mp4 音视频分离的合并留给播放器,
        // P1 只给 progressive(formats);自适应 streams 需 decipher(P2)
        if (pr.streams.isEmpty())
            cb(Result::fail(Err::Parse,
               QXV_STR("youtube: no progressive url (needs decipher)")), {});
        else
            cb(Result::ok(), pr);
    });
}

} // namespace qxv
