#include "douyinprovider.h"
#include "common/qcjson.h"
#include "common/autherror.h"
#include "parse.h"
#include "common/debuglog.h"
#include "common/fingerprint.h"
#include "abogus_js.h"
#include "acsig.h"
#include "acsig_js.h"


// 抖音(2026-11-18 a_bogus 落码后扩面,详见 bytedance.md §13/§15):
//   ✅ 热榜免签:GET /aweme/v1/web/hot/search/list/ → data.word_list[]
//   ✅ search /aweme/v1/web/general/search/single/    a_bogus 签名
//   ✅ detail /aweme/v1/web/aweme/detail/            a_bogus 签名(需 ttwid)
//   ✅ resolve 复用 detail 的 play_addr/bit_rate 档位(5 分钟内同 id 走缓存)
// 失败一律显式 Err(风控/改版→RateLimit/Parse),不返回假数据、不硬崩。

namespace qxv {

static const char* kMod = "douyin";

// ── a_bogus 签名公用件 ──────────────────────────────────────────

// 假 msToken:116 随机字符 + "dhzx" 后缀(服务端统计口径校验,不校验具体值;
// 字符集对齐真实 msToken 的 base64url 口径)。进程内 LCG,种子按秒+地址混合。
static QString fakeMsToken() {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    unsigned int seed = (unsigned int)::time(0)
                      ^ (unsigned int)(unsigned long)&tbl;
    QString s;
    for (int i = 0; i < 116; ++i) {
        seed = seed * 1103515245u + 12345u;
        s += QChar(tbl[(seed >> 16) & 63]);
    }
    s += qFromUtf8("dhzx");
    return s;
}

// 从 chromeWeb 的 UA 提取 Chrome 大版本("Chrome/135." → "135")
static QString chromeMajor(const QString& ua) {
    const int p = qIndexOf(ua, qFromUtf8("Chrome/"));
    if (p < 0) return qFromUtf8("135");
    QString v;
    for (int i = p + 7; i < (int)ua.length() && ua[i] != QChar('.'); ++i)
        v += ua[i];
    return v;
}

// 组签名 URL:queryStr 为完整查询串(已含 msToken,不含 a_bogus)。
// a_bogus 直拼(参考 web 实现不 urlencode;值域为 s4 表字符,实测为准)。
// 签名走 generateChecked:C++ 为准 + quickjs-ng 命令行对照(见 abogus_js.h)。
// 同一请求内 C++/JS 同 seed → 对照可比;seed 取秒级 now,确定性。
static QString signUrl(const QString& base, const QString& queryStr,
                       const QString& ua) {
    const long long nowMs = (long long)::time(0) * 1000;
    const unsigned int seed = (unsigned int)(nowMs / 1000);
    const std::string ab = qxv::abogus::generateChecked(
        std::string(qToUtf8(queryStr).data()),
        std::string(qToUtf8(ua).data()),
        nowMs, seed, 0);
    return base + QChar('?') + queryStr + qFromUtf8("&a_bogus=")
        + QString::fromUtf8(ab.c_str());
}

void DouyinProvider::cancel() { m_net.cancel(); }

void DouyinProvider::homeFeed(int page, SearchCb cb) {
    if (page > 1) {   // 热榜只有一屏(接口无分页参数)
        cb(Result::ok(), {}, false);
        return;
    }
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }

    QUrlQuery q;
    q.addQueryItem(qFromUtf8("device_platform"), qFromUtf8("webapp"));
    q.addQueryItem(qFromUtf8("aid"), qFromUtf8("6383"));
    q.addQueryItem(qFromUtf8("channel"), qFromUtf8("channel_pc_web"));

    NetRequest req;
    req.url = qFromUtf8("https://www.douyin.com/aweme/v1/web/hot/search/list/?%1")
                  .arg(q.toString());
    for (const auto& h : Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9")).headers)
        req.headers.append(h);
    req.headers.append(qMakePair(qFromUtf8("Referer"),
                                 qFromUtf8("https://www.douyin.com/")));
    req.autoCookie = true;
    req.timeoutSec = 20;
    req.maxAttempts = 3;
    req.retryDelayMs = 300;
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    DBG_P(kMod, Debug, "hot search list");

    m_net.send(req, [cb](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        const long long code = jint(root.p, "status_code", -1);
        if (code != 0) {
            cb(withServer(Result::fail(Err::Parse,
                   qFromUtf8("douyin: status_code=%1").arg(code)),
                   200, QString::number(code), QString()), {}, false);
            return;
        }
        const QList36<VideoItem> items = douyin::parse::hotWords(root.p);
        DBG_P(kMod, Debug, "hot list -> %d items", items.size());
        if (items.isEmpty()) {
            cb(Result::fail(Err::RateLimit,
               qFromUtf8("douyin: 热榜空结果(风控或改版)")), {}, false);
            return;
        }
        cb(Result::ok(), items, false);
    });
}

// ── 签名链公用:预热(ttwid) + 设备参数组 + detail 取数 ───────────

// 从 jar 取指定 cookie 值(空=无)。jar 按 host 存,www.douyin.com 统一口径。
static QString cookieValue(const CookieJar& jar, const QString& url,
                           const QString& name) {
    const QString all = jar.cookieForUrl(url);
    if (all.isEmpty()) return QString();
    const QStringList parts = qStrSplit(all, qFromUtf8(";"), true);
    for (int i = 0; i < parts.size(); ++i) {
        const int eq = qIndexOf(parts[i], qFromUtf8("="));
        if (eq <= 0) continue;
        if (qSimplified(parts[i].left(eq)) == name)
            return parts[i].mid(eq + 1);
    }
    return QString();
}

static const char* kDouyinHome = "https://www.douyin.com/";

// ttwid 预热双链(2026-11-18 实测):
//   ① GET 首页 → Set-Cookie __ac_nonce(JS 挑战页,1MB 真页需签名二跳)
//   ② 纯算 __ac_signature(acsig.cpp) 注入 jar → 再 GET 首页
//      → Set-Cookie ttwid + UIFID_TEMP + web_sign_token(.douyin.com 域)
//   ③ 任一环节失败(或拿不到 ttwid)→ 回退官方 union register 口
// done=预热完成回调(失败也回调,由后续实际请求判,不前置失败)。
void DouyinProvider::ensureTtwid(std::function<void()> done) {
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
    // 用户粘贴的登录 cookie(设置页)——search 需登录态(status 2483)。
    // 与 bilibili 同口径:credentials["cookie"] 为浏览器整段 cookie 串。
    if (!m_credImported) {
        m_credImported = true;
        const QString ck = qMapValue(m_cred, qFromUtf8("cookie"),
                                     QVariant()).toString();
        if (!ck.isEmpty()) {
            m_jar.importCookieString(qFromUtf8("www.douyin.com"), ck);
            DBG_P(kMod, Debug, "credential cookie imported (%d chars)",
                  (int)ck.length());
        }
    }
    if (m_ttwidWarm) { done(); return; }
    m_ttwidWarm = true;   // 只预热一次(失败也放行,由实际请求判)
    const Fingerprint fp = Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"));
    NetRequest req;
    req.url = qFromUtf8(kDouyinHome);
    for (const auto& h : fp.headers) req.headers.append(h);
    req.autoCookie = true;
    req.timeoutSec = 15;
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    DBG_P(kMod, Debug, "ttwid warmup 1/2 (nonce)");
    m_net.send(req, [this, fp, done](const NetResponse& r1) {
        const QString home = qFromUtf8(kDouyinHome);
        const QString nonce = cookieValue(m_jar, home, qFromUtf8("__ac_nonce"));
        DBG_P(kMod, Debug, "home1 -> %d nonce=%d", r1.status, (int)nonce.length());
        if (nonce.isEmpty()) { registerTtwid(done); return; }

        // 签名走 acSignatureChecked:C++ 为准 + qjs 命令行对照(acsig_js.h)
        const QString sig = douyin::acSignatureChecked(
            (long long)::time(0), qFromUtf8("www.douyin.com/"), nonce,
            fp.userAgent);
        m_jar.importCookieString(qFromUtf8("www.douyin.com"),
                                 qFromUtf8("__ac_signature=") + sig);
        NetRequest req2;
        req2.url = home;
        for (const auto& h : fp.headers) req2.headers.append(h);
        req2.autoCookie = true;
        req2.timeoutSec = 20;
        if (m_proxy.enabled)
            req2.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
        DBG_P(kMod, Debug, "ttwid warmup 2/2 (signed home)");
        m_net.send(req2, [this, home, done](const NetResponse& r2) {
            const QString tw = cookieValue(m_jar, home, qFromUtf8("ttwid"));
            const QString uf = cookieValue(m_jar, home, qFromUtf8("UIFID_TEMP"));
            DBG_P(kMod, Debug, "home2 -> %d ttwid=%d uifid=%d", r2.status,
                  (int)tw.length(), (int)uf.length());
            if (tw.isEmpty()) registerTtwid(done); else done();
        });
    });
}

// 回退:字节官方 union register 口(Set-Cookie ttwid→callback 下发 .douyin.com)
void DouyinProvider::registerTtwid(std::function<void()> done) {
    NetRequest req;
    req.method = qFromUtf8("POST");
    req.url = qFromUtf8(
        "https://ttwid.bytedance.com/ttwid/union/register/");
    // Qt3 QByteArray 无 const char* 构造,走 compat 转换
    req.body = qToUtf8(qFromUtf8(
        "{\"region\":\"cn\",\"aid\":1768,\"needFid\":false,"
        "\"service\":\"www.douyin.com\",\"migrate_info\":{\"ticket\":\"\","
        "\"source\":\"node\"},\"cbUrlProtocol\":\"https\",\"union\":true}"));
    req.headers.append(qMakePair(qFromUtf8("Content-Type"),
                                 qFromUtf8("application/json")));
    const Fingerprint fp = Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"));
    req.headers.append(qMakePair(qFromUtf8("User-Agent"), fp.userAgent));
    req.autoCookie = true;
    req.timeoutSec = 15;
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    DBG_P(kMod, Debug, "ttwid warmup (union register)");
    m_net.send(req, [done](const NetResponse& r) {
        DBG_P(kMod, Debug, "ttwid register -> %d", r.status);
        done();
    });
}

// 通用设备参数(搜索/详情同款;值与 Chrome web 实测口径一致)
static void addDeviceParams(QUrlQuery* q, const Fingerprint& fp) {
    const QString ver = chromeMajor(fp.userAgent);
    q->addQueryItem(qFromUtf8("device_platform"), qFromUtf8("webapp"));
    q->addQueryItem(qFromUtf8("aid"), qFromUtf8("6383"));
    q->addQueryItem(qFromUtf8("channel"), qFromUtf8("channel_pc_web"));
    q->addQueryItem(qFromUtf8("pc_client_type"), qFromUtf8("1"));
    q->addQueryItem(qFromUtf8("cookie_enabled"), qFromUtf8("true"));
    q->addQueryItem(qFromUtf8("screen_width"), qFromUtf8("1920"));
    q->addQueryItem(qFromUtf8("screen_height"), qFromUtf8("1080"));
    q->addQueryItem(qFromUtf8("browser_language"), qFromUtf8("zh-CN"));
    q->addQueryItem(qFromUtf8("browser_platform"), qFromUtf8("Win32"));
    q->addQueryItem(qFromUtf8("browser_name"), qFromUtf8("Chrome"));
    q->addQueryItem(qFromUtf8("browser_version"), ver + qFromUtf8(".0.0.0"));
    q->addQueryItem(qFromUtf8("browser_online"), qFromUtf8("true"));
    q->addQueryItem(qFromUtf8("engine_name"), qFromUtf8("Blink"));
    q->addQueryItem(qFromUtf8("engine_version"), ver + qFromUtf8(".0.0.0"));
    q->addQueryItem(qFromUtf8("os_name"), qFromUtf8("Windows"));
    q->addQueryItem(qFromUtf8("os_version"), qFromUtf8("10"));
    q->addQueryItem(qFromUtf8("cpu_core_num"), qFromUtf8("16"));
    q->addQueryItem(qFromUtf8("device_memory"), qFromUtf8("8"));
    q->addQueryItem(qFromUtf8("platform"), qFromUtf8("PC"));
    q->addQueryItem(qFromUtf8("downlink"), qFromUtf8("10"));
    q->addQueryItem(qFromUtf8("effective_type"), qFromUtf8("4g"));
    q->addQueryItem(qFromUtf8("round_trip_time"), qFromUtf8("50"));
    q->addQueryItem(qFromUtf8("msToken"), fakeMsToken());
}

static NetRequest signedGet(const QString& base, const QUrlQuery& q,
                            const Fingerprint& fp, const ProxyConfig& proxy,
                            const char* dbgTag) {
    const QString query = q.toString();
    NetRequest req;
    req.url = signUrl(base, query, fp.userAgent);
    for (const auto& h : fp.headers) req.headers.append(h);
    req.headers.append(qMakePair(qFromUtf8("Referer"),
                                 qFromUtf8("https://www.douyin.com/")));
    req.autoCookie = true;
    req.timeoutSec = 20;
    req.maxAttempts = 2;
    req.retryDelayMs = 500;
    if (proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(proxy.host).arg(proxy.port);
    DBG_P(kMod, Debug, "%s signed url len=%d", dbgTag, (int)req.url.length());
    return req;
}

// Argus 签名请求(2026-10 起 detail 类端点必需,yt-dlp #9667 实测口径):
//   uifid(来自 cookie UIFID_TEMP/UIFID)+ timestamp + x-secsdk-web-signature
//   = md5("<uifid>_<ts>_<SALT>_<query>"),query=最终查询串(不含签名自身)。
// 该链不拼 a_bogus(detail 端点上 a_bogus 无关且错值反可能被拒)。
static NetRequest signedGetArgus(const QString& base, QUrlQuery q,
                                 const Fingerprint& fp,
                                 const ProxyConfig& proxy,
                                 const char* dbgTag, const QString& uifid) {
    const QString ts = QString::number((long long)::time(0));
    q.addQueryItem(qFromUtf8("uifid"), uifid);
    q.addQueryItem(qFromUtf8("timestamp"), ts);
    const QString query = q.toString();
    const QString sig = douyin::argusSignature(uifid, ts, query);
    NetRequest req;
    req.url = base + QChar('?') + query
            + qFromUtf8("&x-secsdk-web-signature=") + sig;
    for (const auto& h : fp.headers) req.headers.append(h);
    req.headers.append(qMakePair(qFromUtf8("Referer"),
                                 qFromUtf8("https://www.douyin.com/")));
    req.autoCookie = true;
    req.timeoutSec = 20;
    req.maxAttempts = 2;
    req.retryDelayMs = 500;
    if (proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(proxy.host).arg(proxy.port);
    DBG_P(kMod, Debug, "%s argus url len=%d", dbgTag, (int)req.url.length());
    return req;
}

// detail 取数统一口:带 5 分钟单条缓存(detail→resolve 连击不重打)。
// bodyOk=false 时 cb 收到失败 Result。
void DouyinProvider::fetchDetail(const QString& id,
                                 std::function<void(const Result&,
                                                    const QByteArray&)> cb) {
    if (id.isEmpty()) {
        cb(Result::fail(Err::NotFound, qFromUtf8("douyin: 空 id")), QByteArray());
        return;
    }
    if (id.startsWith(qFromUtf8("hot:"))) {
        cb(Result::fail(Err::NotFound,
           qFromUtf8("douyin: 热词无视频详情,请先搜索")), QByteArray());
        return;
    }
    if (m_cacheId == id &&
        (long long)::time(0) - m_cacheAt < 300) {
        cb(Result::ok(), m_cacheBody);
        return;
    }
    ensureTtwid([this, id, cb]() {
        const Fingerprint fp = Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"));
        const QString home = qFromUtf8(kDouyinHome);
        QString uifid = cookieValue(m_jar, home, qFromUtf8("UIFID_TEMP"));
        if (uifid.isEmpty())
            uifid = cookieValue(m_jar, home, qFromUtf8("UIFID"));
        QUrlQuery q;
        addDeviceParams(&q, fp);
        q.addQueryItem(qFromUtf8("aweme_id"), id);
        const QString base =
            qFromUtf8("https://www.douyin.com/aweme/v1/web/aweme/detail/");
        // 有 uifid → Argus 签名链(2026-10 起 detail 唯一可行面);
        // 无 → 退回 a_bogus 链(旧口径,失败显式报错)
        const NetRequest req = uifid.isEmpty()
            ? signedGet(base, q, fp, m_proxy, "detail")
            : signedGetArgus(base, q, fp, m_proxy, "detail", uifid);
        DBG_P(kMod, Debug, "detail uifid=%d", (int)uifid.length());
        m_net.send(req, [this, id, cb](const NetResponse& r) {
            if (!r.ok()) {
                cb(Result::fail(Err::Network, r.error.isEmpty()
                       ? qFromUtf8("http %1").arg(r.status) : r.error),
                   QByteArray());
                return;
            }
            JRoot root(r.body);
            if (!root.ok()) {
                cb(withServer(Result::fail(Err::Parse, "not json"),
                              r.status, QString(), QString()), QByteArray());
                return;
            }
            // 缓存原始 body(detail/resolve 共用;解析失败也缓存,5 分钟防连击)
            m_cacheId = id;
            m_cacheBody = r.body;
            m_cacheAt = (long long)::time(0);
            cb(Result::ok(), r.body);
        });
    });
}

void DouyinProvider::search(const QString& kw, int page, SearchCb cb) {
    if (qSimplified(kw).isEmpty()) {
        cb(Result::fail(Err::Parse, qFromUtf8("douyin: 空关键词")), {}, false);
        return;
    }
    ensureTtwid([this, kw, page, cb]() {
        const Fingerprint fp = Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"));
        QUrlQuery q;
        addDeviceParams(&q, fp);
        q.addQueryItem(qFromUtf8("search_channel"), qFromUtf8("aweme_video_general"));
        q.addQueryItem(qFromUtf8("sort_type"), qFromUtf8("0"));
        q.addQueryItem(qFromUtf8("publish_time"), qFromUtf8("0"));
        q.addQueryItem(qFromUtf8("keyword"), kw);
        q.addQueryItem(qFromUtf8("search_source"), qFromUtf8("normal_search"));
        q.addQueryItem(qFromUtf8("query_correct_type"), qFromUtf8("1"));
        q.addQueryItem(qFromUtf8("is_filter_search"), qFromUtf8("0"));
        q.addQueryItem(qFromUtf8("from_group_id"), qFromUtf8(""));
        q.addQueryItem(qFromUtf8("offset"), QString::number(20 * (page - 1)));
        q.addQueryItem(qFromUtf8("count"), qFromUtf8("20"));
        q.addQueryItem(qFromUtf8("need_filter_settings"), qFromUtf8("1"));
        q.addQueryItem(qFromUtf8("list_type"), qFromUtf8("multi"));
        q.addQueryItem(qFromUtf8("need_pagination"), qFromUtf8("1"));
        q.addQueryItem(qFromUtf8("need_nearby"), qFromUtf8("0"));
        const QString home = qFromUtf8(kDouyinHome);
        QString uifid = cookieValue(m_jar, home, qFromUtf8("UIFID_TEMP"));
        if (uifid.isEmpty())
            uifid = cookieValue(m_jar, home, qFromUtf8("UIFID"));
        const QString base =
            qFromUtf8("https://www.douyin.com/aweme/v1/web/general/search/single/");
        const NetRequest req = uifid.isEmpty()
            ? signedGet(base, q, fp, m_proxy, "search")
            : signedGetArgus(base, q, fp, m_proxy, "search", uifid);

        m_net.send(req, [cb](const NetResponse& r) {
            if (!r.ok()) {
                cb(Result::fail(Err::Network, r.error.isEmpty()
                       ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
                return;
            }
            JRoot root(r.body);
            if (!root.ok()) {
                cb(withServer(Result::fail(Err::Parse, "not json"),
                              r.status, QString(), QString()), {}, false);
                return;
            }
            const QList36<VideoItem> items = douyin::parse::searchList(root.p);
            const long long code = jint(root.p, "status_code", 0);
            DBG_P(kMod, Debug, "search -> %d items status=%lld",
                  items.size(), code);
            if (items.isEmpty()) {
                // 2026-11 实测:2483 = 匿名搜索已关闭,需登录态 cookie
                if (code == 2483) {
                    cb(withServer(Result::fail(Err::NeedAuth,
                           qFromUtf8("douyin: 搜索需登录(status 2483);"
                                     "请在设置粘贴 douyin.com 的 cookie")),
                           200, QString::number(code), QString()), {}, false);
                    return;
                }
                // 其它 200+空 data:签名无效或风控(参考 §13 实测口径)
                cb(withServer(Result::fail(Err::RateLimit,
                       qFromUtf8("douyin: 搜索空结果(风控或参数改版) status=%1")
                           .arg(code)),
                       200, QString::number(code), QString()), {}, false);
                return;
            }
            cb(Result::ok(), items, items.size() >= 20);
        });
    });
}

void DouyinProvider::detail(const QString& id, DetailCb cb) {
    fetchDetail(id, [cb](const Result& res, const QByteArray& body) {
        if (res.code != Err::Ok) { cb(res, {}, {}); return; }
        JRoot root(body);
        VideoItem vi;
        QString err;
        if (!douyin::parse::awemeDetail(root.p, &vi, &err)) {
            // 典型:ArgusSecurityPlugin 风控 / aweme_detail 缺失
            cb(withServer(Result::fail(Err::RateLimit, err),
                          200, QString(), QString()), {}, {});
            return;
        }
        cb(Result::ok(), vi, {});
    });
}

void DouyinProvider::resolve(const QString& id, ResolveCb cb) {
    resolveQuality(id, QString(), cb);
}

void DouyinProvider::resolveQuality(const QString& id, const QString& quality,
                                    ResolveCb cb) {
    fetchDetail(id, [cb, quality](const Result& res, const QByteArray& body) {
        if (res.code != Err::Ok) { cb(res, PlayResult()); return; }
        JRoot root(body);
        VideoItem vi;
        QString err;
        if (!douyin::parse::awemeDetail(root.p, &vi, &err)) {
            cb(withServer(Result::fail(Err::RateLimit, err),
                          200, QString(), QString()), PlayResult());
            return;
        }
        PlayResult pr;
        pr.item = vi;
        const QList36<Stream> all = douyin::parse::streamsOf(root.p);
        if (all.isEmpty()) {
            cb(Result::fail(Err::NotFound,
               qFromUtf8("douyin: 无可播流(私有编码或下架)")), pr);
            return;
        }
        // 选档:quality 空 = 默认取最低常规档(360P 优先,无则首档);
        // 非空 = 精确匹配;不命中 → 默认档 + degraded 标记(不清给错档)。
        Stream picked;
        int pickIdx = -1;
        for (int i = 0; i < all.size(); ++i) {
            if (!quality.isEmpty() &&
                all[i].quality.compare(quality) == 0) { pickIdx = i; break; }
        }
        if (pickIdx < 0 && quality.isEmpty()) {
            for (int i = 0; i < all.size(); ++i)
                if (all[i].quality.compare(qFromUtf8("360P")) == 0)
                    { pickIdx = i; break; }
        }
        // 列表升序(见 streamsOf):兜底取最低码率档(带宽友好;配置默认 360P)
        if (pickIdx < 0) pickIdx = 0;
        picked = all[pickIdx];
        if (!quality.isEmpty() && picked.quality.compare(quality) != 0) {
            picked.extra.insert(qFromUtf8("requestedQuality"), quality);
            picked.extra.insert(qFromUtf8("degraded"), true);
        }
        // 主流放首,其余按序跟随(供切换清晰度)
        pr.streams.append(picked);
        for (int i = 0; i < all.size(); ++i)
            if (i != pickIdx) pr.streams.append(all[i]);
        cb(Result::ok(), pr);
    });
}

} // namespace qxv
