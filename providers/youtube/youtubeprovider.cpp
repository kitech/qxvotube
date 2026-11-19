#include "compat.h"
#include "youtubeprovider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/fingerprint.h"
#include "common/debuglog.h"
#include "common/potoken.h"
#include "common/qcjson.h"
#include "parse.h"
#include "common/quality.h"
#include "common/ytdlp.h"
#include "common/localstream/localstream.h"
#include "common/autherror.h"

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

void YoutubeProvider::cancel() {
    m_net.cancel();
    m_cache.clear();
}


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
    static const QByteArray k = qbaFromCharptr("\"visitorData\":\"");
    int i = qIndexOf(html, k);
    if (i < 0) return QString();
    i += k.size();
    const int j = qIndexOf(html, qFromUtf8("\""), i);
    if (j < 0) return QString();
    return qFromUtf8(qbaData(qbaMid(html, i, j - i)), j - i);
}

void YoutubeProvider::innertube(const QString& endpoint, const QByteArray& body,
                                const char* ua,
                                std::function<void(const NetResponse&)> cb,
                                const QString& visitorId, bool noCookie) {
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
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
        pre.proxy = effectiveProxy(m_proxy, mustProxy(), kMod);
        m_net.send(pre, [this, endpoint, body, ua, cb, visitorId, noCookie](
                             const NetResponse& wr) {
            // 预热响应里的 visitorData = 本会话访客身份(后续请求必须带上)
            if (m_visitorData.isEmpty())
                m_visitorData = parseVisitorData(wr.body);
            if (!m_visitorData.isEmpty())
                DBG_P(kMod, Debug, "visitorData %d chars", m_visitorData.length());
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
    // 代理来源:QXVOTUBE_PROXY 环境变量(最高) > setProxy();无代理则警告+直连(信任 VPN)
    req.proxy = effectiveProxy(m_proxy, mustProxy(), kMod);
    req.maxAttempts = 3;      // 429/5xx/传输错误统一退避重试
    req.retryDelayMs = 400;   // YouTube 经代理,退避稍长
    DBG_P(kMod, Debug, "POST /%s (%d bytes)", qToUtf8(endpoint).data(), body.size());
    m_net.send(req, std::move(cb));
}

// cJSON 构造 body(自动转义 kw 等),打印后即弃
static QByteArray dumpBody(cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    const QByteArray b = qbaFromCharptr(s);
    cJSON_free(s);
    cJSON_Delete(root);
    return b;
}

// 浏览器 UA(WEB 档 body 用;与 clientName=WEB 匹配)
static const char* kWebUa =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";
// WEB 客户端:仅用于 guide/browse(分类)。
// 2026-11-18 实测(代理出口):guide → Music/Live/Gaming 三个 UC* 主题;
// browse 分别回 lockupViewModel(VIDEO)/videoRenderer/videoCardRenderer。
// ★ body 的 client 与 UA 必须同档(WEB body + Chrome UA),混用恒空结果。
static const YtClient kWebClient = {
    "WEB", "2.20241126.01.00", kWebUa, 0, 0, 0
};

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
                                    qToUtf8(p.visitorData).data());
        cJSON_AddStringToObject(cli, "hl", "en");
        cJSON_AddStringToObject(cli, "gl", "US");
        cJSON* sid = cJSON_AddObjectToObject(root,
                                             "serviceIntegrityDimensions");
        cJSON_AddStringToObject(sid, "poToken", qToUtf8(p.token).data());
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
    cJSON_AddStringToObject(root, "videoId", qToUtf8(videoId).data());
    cJSON_AddBoolToObject(root, "contentCheckOk", 1);
    cJSON_AddBoolToObject(root, "racyCheckOk", 1);
    return dumpBody(root);
}

// 统一填 innertube client 字段(search/continuation/player 三处共用,
// 避免"body 写 WEB、UA 用 Android"这类不同源 bug)
static void addClient(cJSON* cli, const YtClient& c) {
    cJSON_AddStringToObject(cli, "clientName", c.name);
    cJSON_AddStringToObject(cli, "clientVersion", c.version);
    cJSON_AddStringToObject(cli, "userAgent", c.ua);
    if (c.osName) cJSON_AddStringToObject(cli, "osName", c.osName);
    if (c.osVersion) cJSON_AddStringToObject(cli, "osVersion", c.osVersion);
    if (c.sdk > 0) cJSON_AddNumberToObject(cli, "androidSdkVersion", c.sdk);
    cJSON_AddStringToObject(cli, "hl", "en");
    cJSON_AddStringToObject(cli, "gl", "US");
}

// 搜索 body:必须与 UA 同客户端(ANDROID body + Android UA);
// 混用(WEB body + Android UA)实测恒空结果。
static QByteArray searchBody(const QString& kw) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    addClient(cJSON_AddObjectToObject(ctx, "client"), kDefaultClient);
    cJSON_AddStringToObject(root, "query", qToUtf8(kw).data());
    return dumpBody(root);
}


static QByteArray continuationBody(const QString& token) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    addClient(cJSON_AddObjectToObject(ctx, "client"), kDefaultClient);
    cJSON_AddStringToObject(root, "continuation", qToUtf8(token).data());
    return dumpBody(root);
}

// guide/browse 的 WEB body;browseId=0 表示 guide(无 browseId)
static QByteArray webBrowseBody(const char* browseId, const QString& params) {
    cJSON* root = cJSON_CreateObject();
    cJSON* ctx = cJSON_AddObjectToObject(root, "context");
    addClient(cJSON_AddObjectToObject(ctx, "client"), kWebClient);
    if (browseId != 0)
        cJSON_AddStringToObject(root, "browseId", browseId);
    if (!params.isEmpty())
        cJSON_AddStringToObject(root, "params", qToUtf8(params).data());
    return dumpBody(root);
}

// 解析 playabilityStatus:非 OK 转 Result
// ctx:凭证状态;解析结果附 server.code(=status)/server.message(=reason)
static Result playability(const cJSON* root,
                          const AuthContext& ctx = AuthContext()) {
    const cJSON* ps = jobj(root, "playabilityStatus");
    const QString st = jstr(ps, "status");
    if (st == qFromUtf8("OK")) return Result::ok();
    const QString reason = jstr(ps, "reason");
    const QString low = qToLower(reason);
    const QString devMsg =
        qFromUtf8("youtube playability %1: %2").arg(st, reason);

    // 真人识别传导:bot 检查(登录可解)→ NeedAuth/AuthExpired;
    // unusual traffic/captcha(需人机验证)→ CaptchaRequired,detail.captchaUrl
    if (low.contains(qFromUtf8("captcha")) ||
        low.contains(qFromUtf8("unusual traffic")) ||
        low.contains(qFromUtf8("not a bot"))) {
        QVariantMap extra;
        extra.insert(qFromUtf8("captchaUrl"),
                     qFromUtf8("https://www.youtube.com/"));
        return authFail(Err::CaptchaRequired, ctx, devMsg, 200, st, reason, extra);
    }
    if (st.contains(qFromUtf8("LOGIN"))) {
        AuthContext c = ctx;
        c.expired = ctx.provided;
        return authFail(c.provided ? Err::AuthExpired : Err::NeedAuth, c,
                        devMsg, 200, st, reason);
    }
    const Err e = st.contains(qFromUtf8("UNPLAYABLE")) ? Err::GeoBlocked
                                                    : Err::NotFound;
    return withServer(Result::fail(e, devMsg), 200, st, reason);
}

void YoutubeProvider::search(const QString& kw, int page, SearchCb cb) {
    if (page < 1) page = 1;
    // 2026-11-15 定案:搜索走 ANDROID 客户端(compactVideoRenderer),
    // 不需要 visitorData/PO token/cookie;WEB 档在该出口 IP 上恒空结果。
    // 分页:ANDROID 用 continuation token,且 token 只能由上一页给出 → 必须按序翻页。
    // 上游偶发 HTTP 200 + 232B {"error":{"code":500,backendError}} → 重试(最多 3 次);
    // 仍空则显式 Err::RateLimit(不再静默回 0 条)。
    QString contToken;
    if (page > 1) {
        contToken = m_searchTokens[kw][page];   // Qt3 QMap 无 value()
        if (contToken.isEmpty()) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("youtube: 第 %1 页 token 缺失(需按序翻页 1→N)").arg(page)),
               {}, false);
            return;
        }
    }
    struct Ctx { int attempts = 0; std::function<void(int)> go; };
    auto ctx = std::make_shared<Ctx>();
    ctx->go = [this, cb, kw, page, contToken,
               weak = std::weak_ptr<Ctx>(ctx)](int attempt) {
    auto sp = weak.lock();          // 回调期间持有一份,防 Context 提前释放
    if (!sp) return;
    const QByteArray body = contToken.isEmpty() ? searchBody(kw)
                                                : continuationBody(contToken);
    innertube("search", body, kDefaultClient.ua,
      [this, cb, kw, page, sp, attempt](const NetResponse& r) {
        const bool transient = !r.ok() || r.status >= 500
            || r.body.size() < 1024
            || qIndexOf(r.body, qFromUtf8("backendError")) >= 0;
        if (transient && attempt < 3) {
            DBG_P(kMod, Debug, "search transient (status=%d %d bytes) retry %d",
                  r.status, r.body.size(), attempt + 1);
            sp->go(attempt + 1);
            return;
        }
        if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}, false); return; }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        const QList36<VideoItem> items = youtube::parse::searchItems(root.p);
        // 分页:本页响应里的 token 指向 page+1(超过 16 个关键词则清空缓存)
        const QString next = youtube::parse::continuation(root.p);
        if (!next.isEmpty()) {
            if (m_searchTokens.size() > 16) m_searchTokens.clear();
            m_searchTokens[kw][page + 1] = next;
        }
        DBG_P(kMod, Debug, "search '%s' p%d -> %d items (next=%d)",
              qToUtf8(kw).data(), page, items.size(), int(!next.isEmpty()));
        if (items.isEmpty()) {
            DBG_P(kMod, Trace, "search empty body head: %.400s", r.body.data());
            cb(Result::fail(Err::RateLimit,
               qFromUtf8("youtube search 空结果(风控/改版,已重试 %1 次)")
                   .arg(attempt)), {}, false);
            return;
        }
        cb(Result::ok(), items, !next.isEmpty());
    }, QString(), true /* 不带 cookie:避免与访客身份冲突 */);
    };
    ctx->go(1);
}

// ── 分类:首页 ytInitialData 里的 chip bar(动态,不硬编码分类名) ──
// 现状:该出口 IP 的首页是空壳(无 ytInitialData/chips)→ Unsupported;
//      换可达代理后同一段代码自动生效。
static void walkChips(const cJSON* node, QList36<CategoryItem>* out)
{
    if (!node) return;
    if (cJSON_IsObject(node)) {
        const cJSON* chip = cJSON_GetObjectItem(
            const_cast<cJSON*>(node), "chipCloudChipRenderer");
        if (chip) {
            const QString text = jstr(jobj(chip, "text"), "simpleText");
            const QString params =
                jstr(jobj(jobj(chip, "navigationEndpoint"),
                          "browseEndpoint"), "params");
            if (!text.isEmpty() && !params.isEmpty())
                out->append(CategoryItem(params, text));
        }
        for (const cJSON* c = node->child; c; c = c->next)
            walkChips(c, out);
    } else if (cJSON_IsArray(node)) {
        for (const cJSON* c = node->child; c; c = c->next)
            walkChips(c, out);
    }
}

// 从 HTML 里取 var ytInitialData = {...};(花括号配平 + 字符串感知)
static bool extractInitialData(const QByteArray& html, JRoot& out)
{
    const int at = qIndexOf(html, "ytInitialData");
    if (at < 0) return false;
    const int brace = qIndexOf(html, qFromUtf8("{"), at);
    if (brace < 0) return false;
    int depth = 0, end = -1;
    bool inStr = false, esc = false;
    for (int i = brace; i < html.size(); ++i) {
        const char ch = html.at(i);
        if (inStr) {
            if (esc) esc = false;
            else if (ch == '\\') esc = true;
            else if (ch == '"') inStr = false;
            continue;
        }
        if (ch == '"') { inStr = true; continue; }
        if (ch == '{') ++depth;
        else if (ch == '}') { if (--depth == 0) { end = i; break; } }
    }
    if (end < 0) return false;
    out.p = cJSON_ParseWithLengthOpts(html.data() + brace,
                                      (size_t)(end - brace + 1), nullptr, 0);
    return out.ok();
}

void YoutubeProvider::webGetHtml(
    const QString& path,
    std::function<void(const Result&, const QByteArray&)> cb)
{
    NetRequest req;
    req.url = qFromUtf8("https://www.youtube.com%1").arg(path);
    for (const auto& h : Fingerprint::chromeWeb(qFromUtf8("en-US,en")).headers)
        req.headers.append(h);
    req.headers.append(qMakePair(qFromUtf8("Referer"),
                                 qFromUtf8("https://www.youtube.com/")));
    req.timeoutSec = 30;
    req.maxAttempts = 2;
    req.proxy = effectiveProxy(m_proxy, mustProxy(), kMod);
    DBG_P(kMod, Info, "GET %s", qToUtf8(req.url).data());
    m_net.send(req, [cb](const NetResponse& r) {
        if (!r.ok()) {
            DBG_P(kMod, Warn, "html http=%d err=%s", r.status,
                  qToUtf8(r.error).data());
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {});
            return;
        }
        cb(Result::ok(), r.body);
    });
}

void YoutubeProvider::categories(
    std::function<void(const Result&, const QList36<CategoryItem>&)> cb)
{
    // 2026-11-18:首页 HTML/browse(FEwhat_to_watch) 在当前出口 IP 均为空壳,
    // 改走 youtubei/v1/guide(左侧导航,免登录/免 API key,实测可用)。
    innertube("guide", webBrowseBody(0, QString()), kWebClient.ua,
      [cb](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {});
            return;
        }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}); return; }
        const QList36<CategoryItem> items = youtube::parse::guideItems(root.p);
        DBG_P(kMod, Info, "categories(guide) -> %d", items.size());
        if (items.isEmpty()) {
            DBG_P(kMod, Warn, "categories: guide 无 UC* 主题(%d bytes)",
                  r.body.size());
            cb(Result::fail(Err::Unsupported,
               qFromUtf8("youtube: guide 无分类(结构变更/风控)")), {});
            return;
        }
        cb(Result::ok(), items);
      }, QString(), true /* 不带 cookie:游客口径 */);
}

void YoutubeProvider::categoryFeed(const QString& key, int page, SearchCb cb)
{
    if (key.isEmpty()) { homeFeed(page, cb); return; }
    // guide/browse 返回的 UA 页无 continuationItemRenderer(实测) → 不假翻页
    if (page > 1) { cb(Result::ok(), {}, false); return; }
    const int bar = qIndexOf(key, qFromUtf8("|"));
    const QString id = bar > 0 ? key.left(bar) : key;
    const QString params = bar > 0 ? key.mid(bar + 1) : QString();
    innertube("browse", webBrowseBody(qToUtf8(id).data(), params),
              kWebClient.ua,
      [cb](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        JRoot root(r.body);
        if (!root.ok()) {
            cb(Result::fail(Err::Parse, "not json"), {}, false);
            return;
        }
        const QList36<VideoItem> items = youtube::parse::browseItems(root.p);
        DBG_P(kMod, Info, "categoryFeed -> %d items (%d bytes)",
              items.size(), r.body.size());
        if (items.isEmpty()) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("youtube: 分类无条目(空壳/改版)")), {}, false);
            return;
        }
        cb(Result::ok(), items, false);
      }, QString(), true);
}

// ── homeFeed ────────────────────────────────────────────────────────
// 2026-11-15 实测:该出口 IP + ANDROID/WEB 客户端下 browse(FEwhat_to_watch/
// FEtrending) 只回结构壳(0 个 video renderer);/feed/trending 页面同样被风控。
// 与其返回假数据,不如显式告知不可用(UI 可改用 search)。
void YoutubeProvider::homeFeed(int page, SearchCb cb) {
    Q_UNUSED(page);
    cb(Result::fail(Err::Unsupported,
       qFromUtf8("youtube: 首页推荐在当前 IP/客户端下不可用(browse 空壳);"
                 "请改用 search")), {}, false);
}

// 播放列表:id 形如 PL.../UU.../OL.../FL.../RD...(长度 > 11)
static bool looksLikePlaylist(const QString& id) {
    if (id.length() <= 11) return false;
    const QString p = qToUpper(id.left(2));
    return p == qFromUtf8("PL") || p == qFromUtf8("UU") || p == qFromUtf8("OL")
        || p == qFromUtf8("FL") || p == qFromUtf8("RD");
}


void YoutubeProvider::detail(const QString& id, DetailCb cb) {
    // ── 播放列表:返回分集(视频 id 列表) ──────────────────────────
    // 注:ANDROID browse 不带条目标题(只有 watchEndpoint),故 part.title 留空,
    //     由 UI 逐条 detail 补齐(或后续换 WEB/登录态再补)。
    if (looksLikePlaylist(id)) {
        cJSON* root = cJSON_CreateObject();
        cJSON* ctx = cJSON_AddObjectToObject(root, "context");
        cJSON_AddStringToObject(cJSON_AddObjectToObject(ctx, "client"),
                                "clientName", kDefaultClient.name);
        // 复用 addClient 口径(位置在下方 static,这里手填避免前向声明)
        cJSON* cli = jobj(ctx, "client");
        cJSON_AddStringToObject(cli, "clientVersion", kDefaultClient.version);
        cJSON_AddStringToObject(cli, "userAgent", kDefaultClient.ua);
        if (kDefaultClient.osName)
            cJSON_AddStringToObject(cli, "osName", kDefaultClient.osName);
        if (kDefaultClient.osVersion)
            cJSON_AddStringToObject(cli, "osVersion", kDefaultClient.osVersion);
        if (kDefaultClient.sdk > 0)
            cJSON_AddNumberToObject(cli, "androidSdkVersion", kDefaultClient.sdk);
        cJSON_AddStringToObject(cli, "hl", "en");
        cJSON_AddStringToObject(cli, "gl", "US");
        cJSON_AddStringToObject(root, "browseId",
                                qToUtf8(qFromUtf8("VL%1").arg(id)).data());
        const QByteArray body = dumpBody(root);
        innertube("browse", body, kDefaultClient.ua,
            [cb, id](const NetResponse& r) {
            if (!r.ok()) {
                cb(withServer(Result::fail(Err::Network, r.error), r.status,
                              QString(), qbaHead(r.body, 200)),
                   {}, {});
                return;
            }
            JRoot root2(r.body);
            if (!root2.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, {}); return; }
            const QList36<QString> ids = youtube::parse::playlistVideoIds(root2.p, 100);
            if (ids.isEmpty()) {
                cb(Result::fail(Err::Parse,
                   qFromUtf8("youtube: 播放列表无条目(风控/改版)")), {}, {});
                return;
            }
            VideoItem vi;
            vi.id = id;
            vi.title = id;   // 列表标题在 header 渲染器里,ANDROID 未稳定提供
            vi.extra.insert(qFromUtf8("playlist"), true);
            QList36<VideoItem> parts;
            for (int i = 0; i < ids.size(); ++i) {
                VideoItem part;
                part.id = ids[i];
                part.extra.insert(qFromUtf8("playlistId"), id);
                part.extra.insert(qFromUtf8("index"), i);
                parts.append(part);
            }
            DBG_P(kMod, Debug, "playlist %s -> %d items", qToUtf8(id).data(),
                  parts.size());
            cb(Result::ok(), vi, parts);
        }, QString(), true);
        return;
    }

    // ── 单视频 ────────────────────────────────────────────────────
    PotokenSource::fetch([this, id, cb](const Potoken& p) {
        const char* ua = p.valid() ? kWebUa : kClients[0].ua;
        innertube("player", playerBody(id, p), ua,
            [this, cb, id](const NetResponse& r) {
            if (!r.ok()) {
                cb(withServer(Result::fail(Err::Network, r.error), r.status,
                              QString(), qbaHead(r.body, 200)),
                   {}, {});
                return;
            }
            JRoot root(r.body);
            if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, {}); return; }
            Result res = playability(root.p, credContext(m_cred));
            if (res.code != Err::Ok) { cb(res, {}, {}); return; }
            const cJSON* vd = jobj(root.p, "videoDetails");
            VideoItem vi;
            vi.id = jstr(vd, "videoId");
            if (vi.id.isEmpty()) vi.id = id;
            vi.title = jstr(vd, "title");
            vi.author = jstr(vd, "author");
            vi.uid = jstr(vd, "channelId");
            vi.durationMs = jint(vd, "lengthSeconds") * 1000;
            vi.extra.insert(qFromUtf8("viewCount"),
                            (long long)jint(vd, "viewCount"));
            vi.stats.viewCount = jint(vd, "viewCount");
            vi.extra.insert(qFromUtf8("shortDescription"),
                            jstr(vd, "shortDescription"));
            // 注:ANDROID player 响应不含章节(chapter/macroMarkers 全空),
            //     章节需 WEB 渲染器 → 暂不提供(文档已记)。
            { QList36<VideoItem> one; one.append(vi); cb(Result::ok(), vi, one); }   // 单视频
        }, p.visitorData, true);
    });
}

void YoutubeProvider::resolve(const QString& id, ResolveCb cb) {
    resolveQuality(id, QString(), std::move(cb));
}

void YoutubeProvider::resolveQuality(const QString& id, const QString& quality,
                                     ResolveCb cb) {
    // 直链缓存:命中直接返回,过期自动重解析
    const QString ckey = qFromUtf8("youtube|%1|%2").arg(id, quality);
    {
        PlayResult cached;
        if (m_cache.get(ckey, &cached)) {
            DBG_P(kMod, Debug, "resolve cache hit %s", qToUtf8(id).data());
            cb(Result::ok(), cached);
            return;
        }
    }
    const int want = quality::heightOf(quality);
    // >360p 且 yt-dlp 可用 → DASH 合流;其余走 progressive(itag18)
    if (want > 360 && ytdlp::enabled()) {
        resolveDash(id, quality, want, [this, id, quality, cb, ckey](
                                          Result r, const PlayResult& pr) {
            if (r.code == Err::Ok) {
                m_cache.put(ckey, pr, streamcache::ttlFromUrl(
                    pr.streams.first().url, 30 * 60 * 1000));
                cb(r, pr);
                return;
            }
            DBG_P(kMod, Info, "dash failed (%s) -> progressive fallback",
                  qToUtf8(r.message).data());
            resolveProgressive(id, quality, cb);
        });
        return;
    }
    resolveProgressive(id, quality, cb);
}

// 拉 HLS 主播放列表并按请求高度裁剪(保留 #EXT-X-MEDIA 音频轨)
// Qt3 的 QByteArray 无 split/trimmed/startsWith:统一转 QString 处理,末尾回 UTF-8。
static QByteArray trimHlsMaster(const QByteArray& master, int wantHeight) {
    const QString text = qFromUtf8(master.data(), (int)master.size());
    const QStringList lines = qStrSplit(text, qFromUtf8("\n"), false);
    QString chosenInf, chosenUri;
    int best = -1, lowest = -1;
    QString lowestInf, lowestUri;
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const QString l = qTrim(lines[i]);
        if (!l.startsWith(qFromUtf8("#EXT-X-STREAM-INF"))) continue;
        const int rp = qIndexOf(l, qFromUtf8("RESOLUTION="));
        int h = 0;
        if (rp >= 0) {
            const int x = qIndexOf(l, qFromUtf8("x"), rp);
            int e = x + 1;
            while (e < l.length() && l.at(e) >= QChar('0') && l.at(e) <= QChar('9')) ++e;
            if (x > 0 && e > x + 1) h = l.mid(x + 1, e - x - 1).toInt();
        }
        const QString uri = qTrim(lines[i + 1]);
        if (h > 0 && (lowest < 0 || h < lowest)) {
            lowest = h;
            lowestInf = l;
            lowestUri = uri;
        }
        if (wantHeight > 0 && h > wantHeight) continue;
        if (h > best) {
            best = h;
            chosenInf = l;
            chosenUri = uri;
        }
    }
    if (chosenUri.isEmpty()) {   // 请求档低于所有 variant → 用最低档
        chosenInf = lowestInf;
        chosenUri = lowestUri;
    }
    // 重建:所有非 STREAM-INF 行(头/音频声明)+ 选中的 variant
    QString out;
    for (int i = 0; i < lines.size(); ++i) {
        if (!qTrim(lines[i]).startsWith(qFromUtf8("#EXT-X-STREAM-INF")))
            out += lines[i] + qFromUtf8("\n");
    }
    out += chosenInf + qFromUtf8("\n") + chosenUri + qFromUtf8("\n");
    return qToUtf8(out);
}

void YoutubeProvider::resolveDash(const QString& id, const QString& quality,
                                  int wantHeight, ResolveCb cb) {
    const QString url = qFromUtf8("https://www.youtube.com/watch?v=%1").arg(id);
    const QString proxy = effectiveProxy(m_proxy, mustProxy(), kMod);
    DBG_P(kMod, Debug, "dash/hls: yt-dlp fetch %s (want<=%dp)", qToUtf8(id).data(),
          wantHeight);
    ytdlp::fetch(url, proxy, [this, id, quality, wantHeight, proxy, cb](
                                 const ytdlp::Info& info) {
        if (!info.ok || info.hlsMaster.isEmpty()) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("youtube: 无 HLS master(%1)").arg(info.err)), {});
            return;
        }
        // 拉主播放列表(经代理)→ 裁剪 → 交给本地服务,播放器按 HLS 播
        NetRequest pre;
        pre.url = info.hlsMaster;
        pre.headers.append(qMakePair(qFromUtf8("User-Agent"),
                                     QString::fromUtf8(kClients[0].ua)));
        pre.timeoutSec = 25;
        pre.proxy = proxy;
        pre.autoCookie = false;
        pre.maxAttempts = 2;
        if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
        m_net.send(pre, [this, id, quality, wantHeight, proxy, cb](
                             const NetResponse& r) {
            if (!r.ok() || r.body.isEmpty()) {
                cb(Result::fail(Err::Parse,
                   qFromUtf8("youtube: HLS master 拉取失败 status=%1")
                       .arg(r.status)), {});
                return;
            }
            const QByteArray trimmed = trimHlsMaster(r.body, wantHeight);
            const QString key = qFromUtf8("youtube-hls|%1|%2").arg(id).arg(wantHeight);
            auto src = std::make_shared<localstream::BufferSource>(
                trimmed, qFromUtf8("application/vnd.apple.mpegurl"));
            QString localUrl;
            const Result o = localstream::Server::instance().open(key, src,
                                                                  &localUrl);
            if (o.code != Err::Ok) { cb(o, {}); return; }
            PlayResult pr;
            pr.item.id = id;
            Stream st;
            st.url = localUrl;
            st.quality = qFromUtf8("%1P").arg(wantHeight);
            st.kind = qFromUtf8("hls");
            st.extra.insert(qFromUtf8("localProxy"), true);
            st.extra.insert(qFromUtf8("hlsMaster"), true);
            st.extra.insert(qFromUtf8("source"), qFromUtf8("ytdlp"));
            st.extra.insert(qFromUtf8("proxy"), proxy);   // 分片仍走 googlevideo
            st.extra.insert(qFromUtf8("requestedQuality"), quality);
            st.extra.insert(qFromUtf8("userAgent"), QString::fromUtf8(kClients[0].ua));
            pr.streams.append(st);
            DBG_P(kMod, Info, "hls ready: <=%dp (master %d bytes -> %d bytes) %s",
                  wantHeight, r.body.size(), trimmed.size(),
                  qToUtf8(localUrl).data());
            cb(Result::ok(), pr);
        });
    });
}

void YoutubeProvider::resolveProgressive(const QString& id, const QString& quality,
                                        ResolveCb cb) {
    const QString ckey = qFromUtf8("youtube|%1|%2").arg(id, quality);
    PotokenSource::fetch([this, id, cb, quality, ckey](const Potoken& p) {
        // 上游偶发 OK 但 formats 为空(SABR 抖动/该视频仅 adaptive)→ 重试一次,
        // 仍无 progressive 才报 Parse(不静默给空流)。
        struct Ctx { int attempts = 0; std::function<void(int)> go; };
        auto ctx = std::make_shared<Ctx>();
        ctx->go = [this, id, cb, quality, ckey, p,
                   weak = std::weak_ptr<Ctx>(ctx)](int attempt) {
            auto sp = weak.lock();
            if (!sp) return;
            const char* ua = p.valid() ? kWebUa : kClients[0].ua;
            innertube("player", playerBody(id, p), ua,
                [this, id, cb, quality, ckey, sp, attempt](const NetResponse& r) {
                if (!r.ok()) { cb(Result::fail(Err::Network, r.error), {}); return; }
                JRoot root(r.body);
                if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}); return; }
                Result res = playability(root.p, credContext(m_cred));
                if (res.code != Err::Ok) { cb(res, {}); return; }
                PlayResult pr;
                pr.item.id = id;
                cJSON* formats = jarr(jobj(root.p, "streamingData"), "formats");
                for (int i = 0; i < jsize(formats); ++i) {
                    const cJSON* f = jat(formats, i);
                    Stream st;
                    st.url = jstr(f, "url");
                    st.extra.insert(qFromUtf8("referer"),
                                    qFromUtf8("https://www.youtube.com/"));
                    st.extra.insert(qFromUtf8("userAgent"),
                                    qFromUtf8(kClients[0].ua));
                    st.quality = qFromUtf8("%1x%2")
                        .arg(static_cast<int>(jint(f, "width")))
                        .arg(static_cast<int>(jint(f, "height")));
                    st.kind = qFromUtf8("mp4");
                    // 当前只有 itag18(360p);请求任何其它档(含 160/240/480)
                    // → 显式降级标注(请求更低也算不符,省流量意图未满足)
                    if (!qTrim(quality).isEmpty()) {
                        st.extra.insert(qFromUtf8("requestedQuality"), quality);
                        const int got = static_cast<int>(jint(f, "height"));
                        const int want = quality::heightOf(quality);
                        if (want > 0 && got > 0 && want != got) {
                            st.extra.insert(qFromUtf8("degraded"), true);
                            st.extra.insert(qFromUtf8("actualQuality"), st.quality);
                        }
                    }
                    if (!st.url.isEmpty()) pr.streams.append(st);
                }
                if (pr.streams.isEmpty()) {
                    if (attempt < 2) {
                        DBG_P(kMod, Debug, "player no progressive url, retry %d",
                              attempt + 1);
                        sp->go(attempt + 1);
                        return;
                    }
                    cb(Result::fail(Err::Parse,
                       qFromUtf8("youtube: no progressive url (needs decipher)")),
                       {});
                    return;
                }
                m_cache.put(ckey, pr, streamcache::ttlFromUrl(
                    pr.streams.first().url, 2 * 60 * 60 * 1000));
                cb(Result::ok(), pr);
            }, p.visitorData, true);
        };
        ctx->go(1);
    });
}
} // namespace qxv
