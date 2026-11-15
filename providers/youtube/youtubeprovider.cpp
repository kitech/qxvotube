#include "compat.h"
#include "youtubeprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/potoken.h"
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
    const char* osName;
    const char* osVersion;
    int sdk;               // androidSdkVersion(0=不带)
};
// 2026-11-15 实测(该出口 IP):
//   ANDROID 21.26.364 → OK,formats=[itag18 360p mp4 h264+aac 直链](免 PoToken/免 decipher)
//   ANDROID_VR 1.60.19 → LOGIN_REQUIRED "not a bot"(IP 级风控)
//   IOS/VISIONOS → OK 但只有 adaptive 且**无直链**(SABR),需 PO token 才有 url
//   WEB(TVHTML5 等) → UNPLAYABLE/SABR
// 故默认 ANDROID;ANDROID_VR 留作住宅 IP 备选。
static const YtClient kClients[] = {
    { "ANDROID", "21.26.364",
      "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip",
      "Android", "11", 30 },
    { "ANDROID_VR", "1.60.19",
      "com.google.android.apps.youtube.vr.oculus/1.60.19 (Linux; U; Android 12; "
      "eureka-user Build/SQ3A.220605.009.A1) gzip",
      "Android", "12", 0 },
};
static const YtClient& kDefaultClient = kClients[0];

// 从首页 HTML 的 ytcfg 里抓 visitorData(访客身份;必须随 X-Goog-Visitor-Id 头发送)
static QString parseVisitorData(const QByteArray& html) {
    static const QByteArray k("\"visitorData\":\"");
    int i = html.indexOf(k);
    if (i < 0) return QString();
    i += k.size();
    const int j = html.indexOf('"', i);
    if (j < 0) return QString();
    return QString::fromUtf8(html.mid(i, j - i));
}

void YoutubeProvider::innertube(const QString& endpoint, const QByteArray& body,
                                const char* ua,
                                std::function<void(const NetResponse&)> cb,
                                const QString& visitorId, bool noCookie) {
    static NetClient net;
    static CookieJar jar;
    static bool jarInit = (net.setCookieJar(&jar), true);
    static bool warmed = false;
    // player 前必须预热:先 GET 首页种 cookie(YOS/SOCS 等),
    // 否则 playabilityStatus=LOGIN_REQUIRED "confirm you're not a bot"
    if (!warmed) {
        warmed = true;
        NetRequest pre;
        pre.url = qFromUtf8("https://www.youtube.com/");
        // 必须带浏览器 UA:裸 UA(httplib 默认)会拿到风控页并种毒 cookie
        pre.headers = Fingerprint::chromeWeb(qFromUtf8("en-US,en")).headers;
        pre.autoCookie = true;
        pre.proxy = m_proxy.enabled
            ? qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port)
            : QXV_ENV("QXVOTUBE_PROXY");
        net.send(pre, [this, endpoint, body, ua, cb, visitorId, noCookie](
                             const NetResponse& wr) {
            // 预热响应里的 visitorData = 本会话访客身份(后续请求必须带上)
            if (m_visitorData.isEmpty())
                m_visitorData = parseVisitorData(wr.body);
            if (!m_visitorData.isEmpty())
                DBG_P(kMod, Debug, "visitorData %d chars", m_visitorData.size());
            innertube(endpoint, body, ua, cb, visitorId, noCookie);
        });
        return;
    }
    NetRequest req;
    req.url = qFromUtf8(
        "https://www.youtube.com/youtubei/v1/%1?prettyPrint=false").arg(endpoint);
    req.method = qFromUtf8("POST");
    req.body = body;
    req.headers = {
        { "Content-Type", "application/json" },
        { "User-Agent", QString::fromUtf8(ua) },
        { "Origin", "https://www.youtube.com" },
        { "Referer", "https://www.youtube.com/" },
    };
    // 关键:X-Goog-Visitor-Id 缺失 = 空结果/需登录(2026-11-15 实测,
    // 之前误判为"必须 PO token";实际补上访客身份即可)
    const QString vid = visitorId.isEmpty() ? m_visitorData : visitorId;
    if (!vid.isEmpty())
        req.headers.append({ "X-Goog-Visitor-Id", vid });
    // 实测:带上首页种的 cookie 会与访客身份冲突 → search 直接空结果;
    // 播放走 ANDROID 客户端也不需要 cookie(纯游客口径)。
    req.autoCookie = !noCookie;
    // 代理来源:setProxy() 显式设置,或 QXVOTUBE_PROXY=http://host:port(YouTube 被墙直连必超时)
    if (m_proxy.enabled) {
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
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

// 浏览器 UA(WEB 档 body 用;与 clientName=WEB 匹配)
static const char* kWebUa =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";

static QByteArray playerBody(const QString& videoId, const Potoken& p) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    cJSON* cli = cJSON_AddObjectToObject(ctx, "client");
    if (p.valid()) {
        // invidious PR#4789 口径:po_token 仅对 WEB client 生效
        cJSON_AddStringToObject(cli, "clientName", "WEB");
        cJSON_AddStringToObject(cli, "clientVersion", "2.20260101.00.00");
        if (!p.visitorData.isEmpty())
            cJSON_AddStringToObject(cli, "visitorData",
                                    p.visitorData.toUtf8().constData());
        cJSON_AddStringToObject(cli, "hl", "en");
        cJSON_AddStringToObject(cli, "gl", "US");
        cJSON* sid = cJSON_AddObjectToObject(root,
                                             "serviceIntegrityDimensions");
        cJSON_AddStringToObject(sid, "poToken", p.token.toUtf8().constData());
    } else {
        // 默认 ANDROID 客户端(2026-11-15 实测:formats=itag18 360p 直链,
        // 免 PoToken/免 decipher;ANDROID_VR 在该 IP 上被 bot 拦截)
        const YtClient& c = kDefaultClient;
        cJSON_AddStringToObject(cli, "clientName", c.name);
        cJSON_AddStringToObject(cli, "clientVersion", c.version);
        cJSON_AddStringToObject(cli, "userAgent", c.ua);
        if (c.osName)
            cJSON_AddStringToObject(cli, "osName", c.osName);
        if (c.osVersion)
            cJSON_AddStringToObject(cli, "osVersion", c.osVersion);
        if (c.sdk > 0)
            cJSON_AddNumberToObject(cli, "androidSdkVersion", c.sdk);
        cJSON_AddStringToObject(cli, "hl", "en");
        cJSON_AddStringToObject(cli, "gl", "US");
    }
    cJSON_AddStringToObject(root, "videoId", videoId.toUtf8().constData());
    cJSON_AddBoolToObject(root, "contentCheckOk", 1);
    cJSON_AddBoolToObject(root, "racyCheckOk", 1);
    return dumpBody(root);
}

// 搜索 body:必须与 UA 同客户端(ANDROID body + Android UA);
// 混用(WEB body + Android UA)实测恒空结果。
static QByteArray searchBody(const QString& kw) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    cJSON* cli = cJSON_AddObjectToObject(ctx, "client");
    const YtClient& c = kDefaultClient;
    cJSON_AddStringToObject(cli, "clientName", c.name);
    cJSON_AddStringToObject(cli, "clientVersion", c.version);
    cJSON_AddStringToObject(cli, "userAgent", c.ua);
    if (c.osName) cJSON_AddStringToObject(cli, "osName", c.osName);
    if (c.osVersion) cJSON_AddStringToObject(cli, "osVersion", c.osVersion);
    if (c.sdk > 0) cJSON_AddNumberToObject(cli, "androidSdkVersion", c.sdk);
    cJSON_AddStringToObject(cli, "hl", "en");
    cJSON_AddStringToObject(cli, "gl", "US");
    cJSON_AddStringToObject(root, "query", kw.toUtf8().constData());
    return dumpBody(root);
}

// 解析 playabilityStatus:非 OK 转 Result
static Result playability(const cJSON* root) {
    const cJSON* ps = jobj(root, "playabilityStatus");
    const QString st = jstr(ps, "status");
    if (st == qFromUtf8("OK")) return Result::ok();
    const QString reason = jstr(ps, "reason");
    const QString low = reason.toLower();
    Err e = st.contains(qFromUtf8("LOGIN")) ? Err::NeedAuth
          : st.contains(qFromUtf8("UNPLAYABLE")) ? Err::GeoBlocked
          : Err::NotFound;
    // 真人识别传导:bot 检查(登录可解)→ NeedAuth;
    // unusual traffic/captcha(需人机验证)→ CaptchaRequired,detail.captchaUrl
    // 供 UI 弹对话框打开 challenge(§docs/protocols/youtube.md)
    if (low.contains(qFromUtf8("captcha")) ||
        low.contains(qFromUtf8("unusual traffic")) ||
        low.contains(qFromUtf8("not a bot"))) {
        e = Err::CaptchaRequired;
        Result r = Result::fail(e, qFromUtf8("youtube playability %1: %2")
                                    .arg(st, reason));
        r.detail.insert(qFromUtf8("captchaUrl"), qFromUtf8("https://www.youtube.com/"));
        return r;
    }
    return Result::fail(e, qFromUtf8("youtube playability %1: %2")
                            .arg(st, reason));
}

void YoutubeProvider::search(const QString& kw, int page, SearchCb cb) {
    Q_UNUSED(page);   // P1:首页 20 条;分页用 continuationToken(P2)
    // 2026-11-15 定案:搜索走 ANDROID 客户端(compactVideoRenderer),
    // 不需要 visitorData/PO token/cookie;WEB 档在该出口 IP 上恒空结果。
    // 上游偶发 HTTP 200 + 232B {"error":{"code":500,backendError}} → 重试(最多 3 次);
    // 仍空则显式 Err::RateLimit(不再静默回 0 条)。
    struct Ctx { int attempts = 0; std::function<void(int)> go; };
    auto ctx = std::make_shared<Ctx>();
    ctx->go = [this, cb, kw, weak = std::weak_ptr<Ctx>(ctx)](int attempt) {
    auto sp = weak.lock();          // 回调期间持有一份,防 Context 提前释放
    if (!sp) return;
    innertube("search", searchBody(kw), kDefaultClient.ua,
      [this, cb, kw, sp, attempt](const NetResponse& r) {
        const bool transient = !r.ok() || r.status >= 500
            || r.body.size() < 1024
            || r.body.contains(QByteArray("backendError"));
        if (transient && attempt < 3) {
            DBG_P(kMod, Debug, "search transient (status=%d %d bytes) retry %d",
                  r.status, r.body.size(), attempt + 1);
            sp->go(attempt + 1);
            return;
        }
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, false); return; }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        QList<VideoItem> items;
        // 通用递归遍历(结构随版本改版,不写死路径):
        //   WEB  → videoRenderer;ANDROID/IOS → compactVideoRenderer
        // 两者都收(标题/作者/封面字段名不同,分别取)。
        std::function<void(cJSON*)> walk = [&](cJSON* n) {
            if (!n) return;
            if (cJSON_IsObject(n)) {
                if (cJSON* vr = cJSON_GetObjectItemCaseSensitive(n, "videoRenderer")) {
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
                } else if (cJSON* cv = cJSON_GetObjectItemCaseSensitive(
                               n, "compactVideoRenderer")) {
                    if (cJSON_IsObject(cv)) {
                        VideoItem vi;
                        vi.id = jstr(cv, "videoId");
                        const cJSON* title = jobj(cv, "title");
                        vi.title = jstr(title, "simpleText");
                        if (vi.title.isEmpty())
                            vi.title = jstr(jat(jarr(title, "runs"), 0), "text");
                        vi.author = jstr(jat(jarr(jobj(cv, "shortBylineText"), "runs"), 0),
                                         "text");
                        cJSON* thumbs = jarr(jobj(cv, "thumbnail"), "thumbnails");
                        vi.cover = jstr(jat(thumbs, jsize(thumbs) - 1), "url");
                        if (!vi.id.isEmpty()) items.append(vi);
                    }
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
        if (items.isEmpty()) {
            DBG_P(kMod, Trace, "search empty body head: %.400s", r.body.constData());
            cb(Result::fail(Err::RateLimit,
               qFromUtf8("youtube search 空结果(风控/改版,已重试 %1 次)")
                   .arg(attempt)), {}, false);
            return;
        }
        cb(Result::ok(), items, false);
    }, QString(), true /* 不带 cookie:避免与访客身份冲突 */);
    };
    ctx->go(1);
}

void YoutubeProvider::detail(const QString& id, DetailCb cb) {
    PotokenSource::fetch([this, id, cb](const Potoken& p) {
        const char* ua = p.valid() ? kWebUa : kClients[0].ua;
        innertube("player", playerBody(id, p), ua,
            [cb, id](const NetResponse& r) {
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
        }, p.visitorData, true);
    });
}

void YoutubeProvider::resolve(const QString& id, ResolveCb cb) {
    PotokenSource::fetch([this, id, cb](const Potoken& p) {
        const char* ua = p.valid() ? kWebUa : kClients[0].ua;
        innertube("player", playerBody(id, p), ua,
            [cb, id](const NetResponse& r) {
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
            s.quality = qFromUtf8("%1x%2")
                .arg(static_cast<int>(jint(f, "width")))
                .arg(static_cast<int>(jint(f, "height")));
            s.kind = qFromUtf8("mp4");
            if (!s.url.isEmpty()) pr.streams.append(s);
        }
        // 混合流(自适应 DASH)仅取最高一档 mp4 音视频分离的合并留给播放器,
        // P1 只给 progressive(formats);自适应 streams 需 decipher(P2)
        if (pr.streams.isEmpty())
            cb(Result::fail(Err::Parse,
               qFromUtf8("youtube: no progressive url (needs decipher)")), {});
        else
            cb(Result::ok(), pr);
        }, p.visitorData, true);
    });
}

} // namespace qxv
