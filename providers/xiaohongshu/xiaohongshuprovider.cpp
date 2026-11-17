#include "xiaohongshuprovider.h"
#include "common/autherror.h"
#include "common/qcjson.h"
#include "common/debuglog.h"
#include "common/fingerprint.h"
#include "common/quality.h"
#include "api.h"
#include "parse.h"
#include "sign.h"
#include "ssr.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QUrlQuery>
#include <QUrl>

#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

// 口径:docs/providers/xiaohongshu-research.md
// 关键实测:
//   ① 游客 SSR 可用(explore/detail+token/user);
//   ② 详情必须带 xsec_token(按笔记签发,缺失则为空);
//   ③ 搜索/API 一律 -101 无登录信息 → NeedAuth;
//   ④ 风控阈值官方未公开:沿用 XHS-Downloader 默认(均值 6s 对数正态,
//      clamp 1.2~30s),并加每分钟/每日上限与指数冷却。

namespace qxv {

static const char* kMod = "xiaohongshu";
static const char* kSite = "https://www.xiaohongshu.com";

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// std::string ⇄ QString(显式长度,Qt3/Qt6 同源)
QString qsFromStd(const std::string& s) {
    return QString::fromUtf8(s.data(), (int)s.size());
}

VideoItem toVideoItem(const xhsapi::NoteItem& n) {
    VideoItem vi;
    vi.id = qsFromStd(n.id);
    vi.title = qsFromStd(n.title.empty() ? n.desc : n.title);
    vi.author = qsFromStd(n.author);
    vi.uid = qsFromStd(n.authorId);
    vi.cover = qsFromStd(n.cover);
    vi.extra.insert(qFromUtf8("xsecToken"), qsFromStd(n.xsecToken));
    vi.extra.insert(qFromUtf8("xsecSource"), qsFromStd(n.xsecSource));
    vi.extra.insert(qFromUtf8("type"), qsFromStd(n.type));
    vi.extra.insert(qFromUtf8("likedCount"), n.likedCount);
    return vi;
}

// 首页频道(翻页替代):page1 用默认,page>=2 依次换频道
const char* kChannels[] = {
    "homefeed_recommend",
    "homefeed.fashion_v3",
    "homefeed.food_v3",
    "homefeed.travel_v3",
    "homefeed.career_v3",
    "homefeed.movie_and_tv_v3",
};
const int kChannelCount = int(sizeof(kChannels) / sizeof(kChannels[0]));

int envInt(const char* name, int def) {
    if (QXV_ENV_EMPTY(name)) return def;
    bool ok = false;
    const int v = QXV_ENV(name).toInt(&ok);
    return ok ? v : def;
}

// 对数正态延迟:均值 6s(与 XHS-Downloader 默认一致),clamp 1.2~30s
long long jitterDelayMs(int meanMs) {
    static std::mt19937 g(static_cast<unsigned>(std::random_device{}()));
    std::normal_distribution<double> nd(0.0, 0.6);
    const double mean = meanMs / 1000.0;
    const double mu = std::log(mean) - 0.5 * 0.36;
    double sec = std::exp(mu + 0.6 * nd(g));
    const double lo = mean * 0.2, hi = mean * 5.0;
    if (sec < lo) sec = lo;
    if (sec > hi) sec = hi;
    return static_cast<long long>(sec * 1000);
}

} // namespace

XiaohongshuProvider::XiaohongshuProvider() = default;

XiaohongshuProvider::~XiaohongshuProvider() { delete m_signer; }

// ── 凭证 / 登录态 ───────────────────────────────────────────────────
void XiaohongshuProvider::setCredentials(const QVariantMap& c) {
    m_cred = c;
    m_authOk = false;           // 凭证变更 → 重新探测
    m_authCheckedAt = 0;
    m_dryRun = QXV_ENV("QXVOTUBE_XHS_DRYRUN") == qFromUtf8("1");
    if (!hasSession()) {
        if (credContext(m_cred).provided)
            DBG_P(kMod, Warn, "cookie 缺 web_session/a1,登录态不可用");
        return;
    }
    if (!m_signer) m_signer = new xhssign::Signer();
    DBG_P(kMod, Info, "cookie 登录态就绪 (a1=%s)",
          xhssign::cookieA1(cookieStr()).c_str());
}

std::string XiaohongshuProvider::cookieStr() const {
    const QString ck = m_cred.value(qFromUtf8("cookie")).toString();
    return std::string(ck.toUtf8().constData());
}

bool XiaohongshuProvider::hasSession() const {
    return xhssign::hasWebSession(cookieStr());
}

void XiaohongshuProvider::cancel() {
    m_net.cancel();
    m_cache.clear();
}

// ── 节流 gate:所有请求排队(异步,不阻塞 UI)────────────────────────
void XiaohongshuProvider::gate(std::function<void()> fn) {
    const qint64 now = nowMs();
    if (now < m_cooldownUntil) {
        const qint64 left = m_cooldownUntil - now;
        DBG_P(kMod, Info, "cooldown active, drop request (%llds left)",
              (long long)(left / 1000));
        // 冷却期直接失败(调用方 cb 走 RateLimit,这里由各 API 检查后返回)
        QTimer::singleShot(0, [fn]() { fn(); });
        return;
    }
    // 每分钟/每日上限
    if (m_minuteStart == 0 || now - m_minuteStart >= 60000) {
        m_minuteStart = now;
        m_minuteCount = 0;
    }
    if (m_dayStart == 0 || now - m_dayStart >= 86400000) {
        m_dayStart = now;
        m_dayCount = 0;
    }
    const int maxPerMin = envInt("QXVOTUBE_XHS_MAX_PER_MIN", 10);
    const int maxPerDay = envInt("QXVOTUBE_XHS_MAX_PER_DAY", 100);
    if (m_minuteCount >= maxPerMin) {
        const qint64 wait = 60000 - (now - m_minuteStart);
        DBG_P(kMod, Info, "per-minute cap reached, delay %lldms", (long long)wait);
        m_nextAllowed = qMax(m_nextAllowed, now + wait);
    }
    if (m_dayCount >= maxPerDay) {
        DBG_P(kMod, Warn, "per-day cap reached (%d)", m_dayCount);
        return;   // 不再发起(调用方拿到空响应会走错误分支)
    }

    const long long delay = m_nextAllowed > now ? (m_nextAllowed - now) : 0;
    auto run = [this, fn]() {
        m_nextAllowed = nowMs() + jitterDelayMs(
            envInt("QXVOTUBE_XHS_DELAY_MS", 6000));
        ++m_minuteCount;
        ++m_dayCount;
        fn();
    };
    if (delay > 0)
        QTimer::singleShot(int(delay), run);
    else
        run();
}

void XiaohongshuProvider::onSoftBlock() {
    const long long ladder[] = { 5 * 60 * 1000LL, 30 * 60 * 1000LL,
                                 120 * 60 * 1000LL };
    const int idx = qMin(m_blockLevel, 2);
    const qint64 ms = envInt("QXVOTUBE_XHS_COOLDOWN_MS",
                             int(ladder[idx]));
    m_cooldownUntil = nowMs() + ms;
    if (m_blockLevel < 2) ++m_blockLevel;
    DBG_P(kMod, Warn, "soft block detected -> cooldown %llds (level %d)",
          (long long)(ms / 1000), m_blockLevel);
}

QString XiaohongshuProvider::pickToken(const QString& noteId) const {
    auto it = m_tokens.find(noteId);
    if (it == m_tokens.end()) return QString();
    if (nowMs() >= it->expiresAt) return QString();
    return it->token;
}

void XiaohongshuProvider::rememberToken(const QString& noteId,
                                        const QString& token) {
    if (noteId.isEmpty() || token.isEmpty()) return;
    if (m_tokens.size() > 256) m_tokens.clear();
    TokenEntry e;
    e.token = token;
    e.expiresAt = nowMs() + 5 * 60 * 1000;   // 5 分钟(旧链接会被风控)
    m_tokens[noteId] = e;
}

// ── 通用 SSR GET ────────────────────────────────────────────────────
void XiaohongshuProvider::fetchPage(
    const QString& pathWithQuery,
    std::function<void(const Result&, const QByteArray&)> cb) {
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }
    if (nowMs() < m_cooldownUntil) {
        const qint64 left = m_cooldownUntil - nowMs();
        Result r = authFail(Err::RateLimit, credContext(m_cred),
                            qFromUtf8("xiaohongshu: 风控冷却中"));
        r.detail.insert(qFromUtf8("retryAfterMs"), left);
        cb(r, QByteArray());
        return;
    }
    gate([this, pathWithQuery, cb]() {
        NetRequest req;
        req.url = qFromUtf8("%1%2").arg(qFromUtf8(kSite), pathWithQuery);
        for (const auto& h : Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"))
                                 .headers)
            req.headers.append(h);
        req.headers.append(qMakePair(qFromUtf8("Accept"),
            qFromUtf8("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")));
        req.headers.append(qMakePair(qFromUtf8("Accept-Language"),
                                     qFromUtf8("zh-CN,zh;q=0.9,en;q=0.8")));
        req.headers.append(qMakePair(qFromUtf8("Referer"),
                                     qFromUtf8("https://www.xiaohongshu.com/")));
        req.autoCookie = true;
        req.timeoutSec = 25;
        req.maxAttempts = 2;      // 风控环境下少重试,避免加重
        req.retryDelayMs = 2000;
        if (m_proxy.enabled)
            req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
        DBG_P(kMod, Debug, "GET %s", qPrintable(req.url));
        m_net.send(req, [this, cb](const NetResponse& r) {
            if (r.status == 302) {   // 跳登录
                onSoftBlock();
                QVariantMap extra;
                extra.insert(qFromUtf8("captchaUrl"),
                             qFromUtf8("https://www.xiaohongshu.com/explore"));
                cb(authFail(Err::CaptchaRequired, credContext(m_cred),
                            qFromUtf8("xiaohongshu: 被重定向到登录/验证"),
                            r.status, QString(), QString(), extra),
                   QByteArray());
                return;
            }
            if (r.status == 461 || r.status == 406) {
                onSoftBlock();
                QVariantMap extra;
                extra.insert(qFromUtf8("retryAfterMs"),
                             m_cooldownUntil - nowMs());
                cb(authFail(Err::RateLimit, credContext(m_cred),
                            qFromUtf8("xiaohongshu: 风控(%1)").arg(r.status),
                            r.status, QString::number(r.status),
                            QString::fromUtf8(r.body.left(200)), extra),
                   QByteArray());
                return;
            }
            if (!r.ok()) {
                cb(authFail(Err::Network, credContext(m_cred),
                            r.error.isEmpty()
                                ? qFromUtf8("http %1").arg(r.status) : r.error,
                            r.status, QString(),
                            QString::fromUtf8(r.body.left(200))), QByteArray());
                return;
            }
            cb(Result::ok(), r.body);
        });
    });
}

// ── 服务端错误 → Result(认证/风控/其它;原始响应入 detail)─────────────
Result XiaohongshuProvider::mapApiFailure(int httpStatus,
                                          const QByteArray& body) {
    const AuthContext ctx = credContext(m_cred);
    QString code, msg;
    {
        JRoot root(body);
        if (root.ok()) {
            code = QString::number(jint(root.p, "code", 0));
            msg = jstr(root.p, "msg");
            if (msg.isEmpty()) msg = jstr(root.p, "message");
        }
    }
    if (code == qFromUtf8("-101") || httpStatus == 401) {
        AuthContext c = ctx;
        c.expired = ctx.provided;
        return authFail(Err::AuthExpired, c,
                        qFromUtf8("xiaohongshu: 登录态失效(-101)"),
                        httpStatus, code, msg);
    }
    if (httpStatus == 461 || httpStatus == 406 || httpStatus == 429) {
        onSoftBlock();
        QVariantMap extra;
        extra.insert(qFromUtf8("retryAfterMs"), m_cooldownUntil - nowMs());
        return authFail(Err::RateLimit, ctx,
                        qFromUtf8("xiaohongshu: API 风控(%1)").arg(httpStatus),
                        httpStatus, code, msg, extra);
    }
    const QString raw = QString::fromUtf8(body.left(200));
    return withServer(
        Result::fail(Err::Network,
                     qFromUtf8("xiaohongshu: API %1").arg(httpStatus)),
        httpStatus, code, msg.isEmpty() ? raw : msg);
}

// ── 带签名的 API 请求(edith.xiaohongshu.com)────────────────────────
void XiaohongshuProvider::apiRequest(
    const QByteArray& method, const QString& path, const QByteArray& body,
    std::function<void(const Result&, const QByteArray&)> cb) {
    const AuthContext ctx = credContext(m_cred);
    if (!hasSession() || !m_signer) {
        cb(authFail(Err::NeedAuth, ctx,
                    ctx.provided
                        ? qFromUtf8("xiaohongshu: cookie 缺 web_session/a1,"
                                    "无法生成 x-s 签名")
                        : qFromUtf8("xiaohongshu: 未设置登录凭证")),
           QByteArray());
        return;
    }
    if (nowMs() < m_cooldownUntil) {
        Result r = authFail(Err::RateLimit, ctx,
                            qFromUtf8("xiaohongshu: 风控冷却中"));
        r.detail.insert(qFromUtf8("retryAfterMs"), m_cooldownUntil - nowMs());
        cb(r, QByteArray());
        return;
    }
    const bool isPost = method.toUpper() == "POST";
    // 签名(纯 C++;uri 不含 query)
    const std::string uriStd(path.toUtf8().constData());
    const std::string a1 = xhssign::cookieA1(cookieStr());
    const std::string payloadStd =
        isPost ? std::string(body.constData(), (size_t)body.size())
               : std::string();
    const std::string xs =
        m_signer->signXs(isPost ? "POST" : "GET", uriStd, a1, payloadStd);
    const std::string xsc = m_signer->signXsCommon(a1);
    char xtBuf[32];
    snprintf(xtBuf, sizeof(xtBuf), "%lld",
             (long long)(nowMs() > 0 ? nowMs() : 0));
    const std::string xt = xtBuf;
    const std::string b3 = m_signer->b3TraceId();
    if (m_dryRun)
        DBG_P(kMod, Info, "DRYRUN sig x-s=%zuB x-s-common=%zuB (%.12s…)",
              xs.size(), xsc.size(), xs.c_str());

    gate([this, isPost, path, body, xs, xsc, xt, b3, cb]() {
        NetRequest req;
        req.url = qFromUtf8("https://edith.xiaohongshu.com%1").arg(path);
        req.method = isPost ? qFromUtf8("POST") : qFromUtf8("GET");
        if (!body.isEmpty()) req.body = body;
        for (const auto& h : Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9"))
                                 .headers)
            req.headers.append(h);
        req.headers.append(qMakePair(qFromUtf8("x-s"),
                                     QString::fromUtf8(xs.c_str())));
        req.headers.append(qMakePair(qFromUtf8("x-s-common"),
                                     QString::fromUtf8(xsc.c_str())));
        req.headers.append(qMakePair(qFromUtf8("x-t"),
                                     QString::fromUtf8(xt.c_str())));
        req.headers.append(qMakePair(qFromUtf8("x-b3-traceid"),
                                     QString::fromUtf8(b3.c_str())));
        req.headers.append(qMakePair(qFromUtf8("x-mns"),
                                     qFromUtf8("unload")));
        req.headers.append(qMakePair(qFromUtf8("Content-Type"),
                                     qFromUtf8("application/json;charset=UTF-8")));
        req.headers.append(qMakePair(qFromUtf8("Origin"),
                                     qFromUtf8("https://www.xiaohongshu.com")));
        req.headers.append(qMakePair(qFromUtf8("Referer"),
                                     qFromUtf8("https://www.xiaohongshu.com/")));
        req.autoCookie = true;
        req.timeoutSec = 25;
        req.maxAttempts = 1;      // 签名请求不盲目重试
        if (m_proxy.enabled)
            req.proxy = qFromUtf8("http://%1:%2")
                            .arg(m_proxy.host).arg(m_proxy.port);
        DBG_P(kMod, Debug, "API %s %s", qPrintable(req.method),
              qPrintable(req.url));
        m_net.send(req, [this, cb](const NetResponse& r) {
            const QString cap = QXV_ENV("QXVOTUBE_XHS_CAPTURE");
            if (!cap.isEmpty() && !r.body.isEmpty()) {
                QDir().mkpath(cap);
                QFile f(cap + qFromUtf8("/%1_%2.json")
                                  .arg(r.status).arg(nowMs()));
                if (f.open(QIODevice::WriteOnly)) f.write(r.body);
            }
            if (r.status == 200) {
                JRoot root(r.body);
                if (root.ok() && (int)jint(root.p, "code", 0) == 0) {
                    cb(Result::ok(), r.body);
                    return;
                }
            }
            cb(mapApiFailure(r.status, r.body), QByteArray());
        });
    });
}

// ── 登录态探测:GET /api/sns/web/v1/user/selfinfo(游客 → -101)────────
void XiaohongshuProvider::ensureAuth(std::function<void(const Result&)> cb) {
    if (m_authOk && nowMs() - m_authCheckedAt < 5 * 60 * 1000) {
        cb(Result::ok());
        return;
    }
    apiRequest(qFromUtf8("GET").toUtf8(),
               qFromUtf8("/api/sns/web/v1/user/selfinfo"), QByteArray(),
               [this, cb](const Result& r, const QByteArray& body) {
        Q_UNUSED(body);
        if (r.code == Err::Ok) {
            m_authOk = true;
            m_authCheckedAt = nowMs();
        }
        cb(r);
    });
}

// ── search:必须登录(无免登录通道)─────────────────────────────────
void XiaohongshuProvider::search(const QString& kw, int page, SearchCb cb) {
    Q_UNUSED(kw);
    Q_UNUSED(page);
    const AuthContext ctx = credContext(m_cred);
    if (!hasSession()) {   // 游客:免登录无通道(实测 -101)
        cb(authFail(Err::NeedAuth, ctx,
           ctx.provided
               ? qFromUtf8("xiaohongshu: 已设置凭证但缺 web_session/a1")
               : qFromUtf8("xiaohongshu: 搜索需登录(官方 web API -101);"
                           "游客可用首页/用户页/笔记详情"),
           200, qFromUtf8("-101"), qFromUtf8("无登录信息")), {}, false);
        return;
    }
    // 登录态:探测后走 web API(schema 派生解析,见 api.h 说明)
    ensureAuth([this, kw, page, cb](const Result& a) {
        if (a.code != Err::Ok) { cb(a, {}, false); return; }
        const std::string body = xhsapi::buildSearchBody(
            std::string(kw.toUtf8().constData()), page, 20, xhsapi::searchId(),
            "general", 0);
        apiRequest(qFromUtf8("POST").toUtf8(),
                   qFromUtf8("/api/sns/web/v1/search/notes"),
                   QByteArray(body.c_str(), (int)body.size()),
                   [cb](const Result& r, const QByteArray& b) {
            if (r.code != Err::Ok) { cb(r, {}, false); return; }
            const xhsapi::PagedNotes p = xhsapi::parseSearch(
                std::string(b.constData(), (size_t)b.size()));
            QList<VideoItem> out;
            for (size_t i = 0; i < p.items.size(); ++i)
                out.append(toVideoItem(p.items[i]));
            cb(Result::ok(), out, p.hasMore);
        });
    });
}

// ── 分类:从 explore SSR 动态探测(无硬编码频道表) ────────────────
// 现状:游客 explore 常被风控(无 __INITIAL_STATE__)→ Unsupported;
//      拿到登录态/cookie 后同一段代码自动生效,UI 无需改动。
void XiaohongshuProvider::categories(
    std::function<void(const Result&, const QList<CategoryItem>&)> cb) {
    DBG_P(kMod, Info, "categories: GET /explore (probe SSR channels)");
    fetchPage(qFromUtf8("/explore"),
              [cb](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) {
            DBG_P(kMod, Warn, "categories: explore failed code=%d (%d bytes)",
                  (int)r.code, body.size());
            cb(r, {});
            return;
        }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            DBG_P(kMod, Warn, "categories: 无 __INITIAL_STATE__(风控,%d bytes)",
                  body.size());
            cb(Result::fail(Err::Unsupported,
               qFromUtf8("xiaohongshu: 游客无法读取频道(风控)")), {});
            return;
        }
        QList<CategoryItem> cs = xiaohongshu::parse::channelList(root.p);
        DBG_P(kMod, Info, "categories: %d channels from SSR", cs.size());
        if (cs.isEmpty()) {
            DBG_P(kMod, Warn, "categories: SSR 无频道字段(候选键均未命中)");
            cb(Result::fail(Err::Unsupported,
               qFromUtf8("xiaohongshu: SSR 未暴露频道列表")), {});
            return;
        }
        cs.prepend(CategoryItem(QString(), QString()));   // 推荐
        cb(Result::ok(), cs);
    });
}

void XiaohongshuProvider::categoryFeed(const QString& key, int page,
                                       SearchCb cb) {
    if (key.isEmpty()) { homeFeed(page, cb); return; }
    // 频道 = /explore?channel_id=<key>(复用 homeFeed 的 SSR 解析链路)
    const QString path = qFromUtf8("/explore?channel_id=%1")
                             .arg(QString(QUrl::toPercentEncoding(key)));
    DBG_P(kMod, Info, "categoryFeed: channel=%s p%d", qPrintable(key), page);
    fetchPage(path, [this, cb, key](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) {
            DBG_P(kMod, Warn, "categoryFeed channel=%s failed code=%d",
                  qPrintable(key), (int)r.code);
            cb(r, {}, false);
            return;
        }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            DBG_P(kMod, Warn, "categoryFeed channel=%s 无 SSR(风控)",
                  qPrintable(key));
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 频道页无 SSR(风控)")), {}, false);
            return;
        }
        const QList<VideoItem> items = xiaohongshu::parse::feedNotes(root.p);
        DBG_P(kMod, Debug, "categoryFeed channel=%s -> %d items",
              qPrintable(key), items.size());
        if (items.isEmpty())
            DBG_P(kMod, Warn, "categoryFeed channel=%s empty", qPrintable(key));
        cb(Result::ok(), items, false);   // SSR 单屏
    });
}

// ── homeFeed:explore + 频道轮转 ────────────────────────────────────
void XiaohongshuProvider::homeFeed(int page, SearchCb cb) {
    if (page < 1) page = 1;
    QString path = qFromUtf8("/explore");
    if (page > 1) {
        if (page - 2 >= kChannelCount) {
            cb(Result::ok(), {}, false);
            return;
        }
        path += qFromUtf8("?channel_id=%1")
                    .arg(QString::fromUtf8(kChannels[page - 2]));
    }
    fetchPage(path, [this, cb, page](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) { cb(r, {}, false); return; }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();   // 无 __INITIAL_STATE__ = 反爬降级
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 首页无 __INITIAL_STATE__(风控)"),
                        200, QString(),
                        QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        const QList<VideoItem> items = xiaohongshu::parse::feedNotes(root.p);
        if (items.isEmpty()) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 首页 feed 为空(风控)"),
                        200, QString(),
                        QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        for (const VideoItem& it : items)
            rememberToken(it.id,
                          it.extra.value(qFromUtf8("xsecToken")).toString());
        const bool hasMore = page < kChannelCount + 1;
        DBG_P(kMod, Info, "homeFeed p%d -> %d items (hasMore=%d)", page,
              items.size(), int(hasMore));
        cb(Result::ok(), items, hasMore);
    });
}

// ── detail:必须 token ──────────────────────────────────────────────
void XiaohongshuProvider::detail(const QString& id, DetailCb cb) {
    QString noteId = id, token;
    if (id.contains(QLatin1Char('@'))) {
        const int at = id.indexOf(QLatin1Char('@'));
        noteId = id.left(at);
        token = id.mid(at + 1);
    } else if (id.startsWith(qFromUtf8("http"))) {
        QString t;
        xiaohongshu::parse::parseShareUrl(id, &noteId, &t);
        token = t;
    }
    if (token.isEmpty()) token = pickToken(noteId);
    if (noteId.isEmpty() || token.isEmpty()) {
        cb(authFail(Err::NeedAuth, credContext(m_cred),
           qFromUtf8("xiaohongshu: 缺 xsec_token(从首页/用户页打开,"
                     "或粘贴分享链接;id 可写 noteId@token)")), {}, {});
        return;
    }
    QUrlQuery q;
    q.addQueryItem(qFromUtf8("xsec_token"), token);
    q.addQueryItem(qFromUtf8("xsec_source"), qFromUtf8("pc_feed"));
    const QString path = qFromUtf8("/explore/%1?%2")
                             .arg(noteId, q.toString(QUrl::FullyEncoded));
    fetchPage(path, [this, cb, noteId, token](const Result& r,
                                              const QByteArray& body) {
        auto apiFallback = [this, cb, noteId, token]() {
            resolveByApi(noteId, token,
                         [cb](const Result& ar, const PlayResult* pr) {
                if (ar.code != Err::Ok || !pr) { cb(ar, {}, {}); return; }
                QList<VideoItem> parts;
                parts.append(pr->item);
                cb(Result::ok(), pr->item, parts);
            });
        };
        if (r.code != Err::Ok) {
            if (hasSession()) { apiFallback(); return; }
            cb(r, {}, {});
            return;
        }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 详情无 SSR(风控)"), 200,
                        QString(), QString::fromUtf8(body.left(200))), {}, {});
            return;
        }
        VideoItem vi;
        QList<Stream> streams;
        const Result res =
            xiaohongshu::parse::noteDetail(root.p, noteId, &vi, &streams);
        if (res.code == Err::Parse) {
            onSoftBlock();   // detailMap 空 = 软风控/token 过期
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 详情为空(token 过期或风控)"),
                        200, QString(),
                        QString::fromUtf8(body.left(200))), {}, {});
            return;
        }
        QList<VideoItem> parts;   // 小红书无分集
        if (res.code == Err::Ok) parts.append(vi);
        cb(res, vi, parts);
    });
}

// ── resolve ────────────────────────────────────────────────────────
void XiaohongshuProvider::resolve(const QString& id, ResolveCb cb) {
    const QString ckey = qFromUtf8("xiaohongshu|%1").arg(id);
    PlayResult cached;
    if (m_cache.get(ckey, &cached)) { cb(Result::ok(), cached); return; }

    // 解析 id/token(与 detail 同口径),只发一次请求
    QString noteId = id, token;
    if (id.contains(QLatin1Char('@'))) {
        const int at = id.indexOf(QLatin1Char('@'));
        noteId = id.left(at);
        token = id.mid(at + 1);
    } else if (id.startsWith(qFromUtf8("http"))) {
        QString t;
        xiaohongshu::parse::parseShareUrl(id, &noteId, &t);
        token = t;
    }
    if (token.isEmpty()) token = pickToken(noteId);
    if (noteId.isEmpty() || token.isEmpty()) {
        cb(authFail(Err::NeedAuth, credContext(m_cred),
           qFromUtf8("xiaohongshu: 缺 xsec_token(从首页点开或粘贴分享链接)")), {});
        return;
    }
    QUrlQuery q;
    q.addQueryItem(qFromUtf8("xsec_token"), token);
    q.addQueryItem(qFromUtf8("xsec_source"), qFromUtf8("pc_feed"));
    const QString path = qFromUtf8("/explore/%1?%2")
                             .arg(noteId, q.toString(QUrl::FullyEncoded));
    fetchPage(path, [this, cb, noteId, token, ckey](const Result& r,
                                                    const QByteArray& body) {
        // SSR 链路失败且登录态 → API feed 兜底(错误语义不变,只多一次机会)
        auto apiFallback = [this, cb, noteId, token, ckey]() {
            resolveByApi(noteId, token,
                         [this, cb, ckey](const Result& ar,
                                          const PlayResult* pr) {
                if (ar.code != Err::Ok || !pr) { cb(ar, {}); return; }
                m_cache.put(ckey, *pr, 10 * 60 * 1000);
                DBG_P(kMod, Info, "resolve %s via api -> 1 stream", "api");
                cb(Result::ok(), *pr);
            });
        };
        if (r.code != Err::Ok) {
            if (hasSession()) { apiFallback(); return; }
            cb(r, {});
            return;
        }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            if (hasSession()) { apiFallback(); return; }
            cb(Result::fail(Err::RateLimit,
                            qFromUtf8("xiaohongshu: 详情无 SSR(风控)")), {});
            return;
        }
        VideoItem vi;
        QList<Stream> streams;
        const Result res =
            xiaohongshu::parse::noteDetail(root.p, noteId, &vi, &streams);
        if (res.code == Err::Parse) {
            onSoftBlock();
            if (hasSession()) { apiFallback(); return; }
            cb(Result::fail(Err::RateLimit,
                            qFromUtf8("xiaohongshu: 详情为空(token 过期或风控)")),
               {});
            return;
        }
        if (res.code != Err::Ok) { cb(res, {}); return; }
        PlayResult pr;
        pr.item = vi;
        pr.streams = streams;
        m_cache.put(ckey, pr, 10 * 60 * 1000);
        DBG_P(kMod, Info, "resolve %s -> %d streams", qPrintable(noteId),
              pr.streams.size());
        cb(Result::ok(), pr);
    });
}

// ── 登录态 feed 兜底(SSR 拿不到时)──────────────────────────────────
void XiaohongshuProvider::resolveByApi(
    const QString& noteId, const QString& token,
    std::function<void(const Result&, const PlayResult*)> cb) {
    const std::string body = xhsapi::buildFeedBody(
        std::string(noteId.toUtf8().constData()), "pc_feed",
        std::string(token.toUtf8().constData()));
    apiRequest(qFromUtf8("POST").toUtf8(), qFromUtf8("/api/sns/web/v1/feed"),
               QByteArray(body.c_str(), (int)body.size()),
               [cb](const Result& r, const QByteArray& b) {
        if (r.code != Err::Ok) { cb(r, nullptr); return; }
        const xhsapi::NoteDetail d = xhsapi::parseFeed(
            std::string(b.constData(), (size_t)b.size()));
        if (d.streamUrl.empty()) {
            cb(withServer(Result::fail(Err::Unsupported,
                   qFromUtf8("xiaohongshu: 无视频流(图文笔记或结构变更)")),
                   200, QString(), QString()), nullptr);
            return;
        }
        PlayResult pr;
        pr.item = toVideoItem(d.note);
        pr.item.durationMs = d.durationMs;
        Stream st;
        st.url = qsFromStd(d.streamUrl);
        st.kind = qFromUtf8("mp4");
        st.quality = quality::labelFor(d.height);
        QStringList backup;
        for (size_t i = 0; i < d.backupUrls.size(); ++i)
            backup.append(qsFromStd(d.backupUrls[i]));
        st.extra.insert(qFromUtf8("backupUrls"), backup);
        st.extra.insert(qFromUtf8("referer"),
                        qFromUtf8("https://www.xiaohongshu.com/"));
        st.extra.insert(qFromUtf8("source"), qFromUtf8("api"));
        pr.streams.append(st);
        cb(Result::ok(), &pr);
    });
}

void XiaohongshuProvider::resolveQuality(const QString& id,
                                         const QString& quality,
                                         ResolveCb cb) {
    // 小红书每篇笔记视频档位固定(通常 1080x720/单档)→ 忽略 quality 请求
    Q_UNUSED(quality);
    resolve(id, std::move(cb));
}

// ── userVideos:用户页 SSR(可能无 id,见调研)───────────────────────
void XiaohongshuProvider::userVideos(const QString& uid, int page, SearchCb cb) {
    if (hasSession()) {   // 登录态:GET /user_posted,游标分页
        if (page <= 1) m_userCursor.clear();
        std::vector<std::pair<std::string, std::string> > kv;
        kv.push_back(std::make_pair(std::string("num"), std::string("30")));
        kv.push_back(std::make_pair(
            std::string("cursor"),
            std::string(m_userCursor.toUtf8().constData())));
        kv.push_back(std::make_pair(std::string("user_id"),
                                    std::string(uid.toUtf8().constData())));
        kv.push_back(std::make_pair(std::string("image_formats"),
                                    std::string("jpg,webp,avif")));
        kv.push_back(std::make_pair(std::string("xsec_source"),
                                    std::string("pc_feed")));
        const std::string qs = xhsapi::queryEncode(kv);
        ensureAuth([this, qs, cb](const Result& a) {
            if (a.code != Err::Ok) { cb(a, {}, false); return; }
            apiRequest(qFromUtf8("GET").toUtf8(),
                       qFromUtf8("/api/sns/web/v1/user_posted?")
                           .append(qsFromStd(qs)),
                       QByteArray(),
                       [this, cb](const Result& r, const QByteArray& b) {
                if (r.code != Err::Ok) { cb(r, {}, false); return; }
                const xhsapi::PagedNotes p = xhsapi::parseUserPosted(
                    std::string(b.constData(), (size_t)b.size()));
                m_userCursor = qsFromStd(p.cursor);
                QList<VideoItem> out;
                for (size_t i = 0; i < p.items.size(); ++i) {
                    const VideoItem vi = toVideoItem(p.items[i]);
                    rememberToken(vi.id,
                        vi.extra.value(qFromUtf8("xsecToken")).toString());
                    out.append(vi);
                }
                cb(Result::ok(), out, p.hasMore);
            });
        });
        return;
    }
    if (page > 1) {   // 游客:SSR 首屏 30 条,无分页参数
        cb(Result::ok(), {}, false);
        return;
    }
    fetchPage(qFromUtf8("/user/profile/%1").arg(uid),
              [this, cb, uid](const Result& r, const QByteArray& body) {
        if (r.code != Err::Ok) { cb(r, {}, false); return; }
        JRoot root;
        if (!xiaohongshu::ssr::extractInitialState(body, root)) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 用户页无 SSR(风控)"), 200,
                        QString(), QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        QList<VideoItem> items = xiaohongshu::parse::userNotes(root.p);
        if (items.isEmpty()) {
            onSoftBlock();
            cb(authFail(Err::RateLimit, credContext(m_cred),
                        qFromUtf8("xiaohongshu: 用户页无笔记(风控/隐私)"), 200,
                        QString(), QString::fromUtf8(body.left(200))), {}, false);
            return;
        }
        for (const VideoItem& it : items)
            rememberToken(it.id,
                          it.extra.value(qFromUtf8("xsecToken")).toString());
        DBG_P(kMod, Info, "userVideos %s -> %d items", qPrintable(uid),
              items.size());
        cb(Result::ok(), items, false);
    });
}

// ── comments:SSR 首屏(若有)───────────────────────────────────────
void XiaohongshuProvider::comments(const QString& id, const QString& cursor,
                                   CommentsCb cb) {
    QString noteId = id, token;
    if (id.contains(QLatin1Char('@'))) {
        const int at = id.indexOf(QLatin1Char('@'));
        noteId = id.left(at);
        token = id.mid(at + 1);
    } else if (id.startsWith(qFromUtf8("http"))) {
        QString t;
        xiaohongshu::parse::parseShareUrl(id, &noteId, &t);
        token = t;
    }
    if (token.isEmpty()) token = pickToken(noteId);
    if (!hasSession() || noteId.isEmpty()) {   // 游客:SSR 首屏已在 detail
        cb(Result::fail(Err::Unsupported,
           qFromUtf8("xiaohongshu: 评论仅详情 SSR 首屏;"
                     "单独评论列表需登录态")),
           {}, QString());
        return;
    }
    std::vector<std::pair<std::string, std::string> > kv;
    kv.push_back(std::make_pair(std::string("note_id"),
                                std::string(noteId.toUtf8().constData())));
    kv.push_back(std::make_pair(std::string("cursor"),
                                std::string(cursor.toUtf8().constData())));
    kv.push_back(std::make_pair(std::string("top_comment_id"), std::string()));
    kv.push_back(std::make_pair(std::string("image_formats"),
                                std::string("jpg,webp,avif")));
    kv.push_back(std::make_pair(std::string("xsec_token"),
                                std::string(token.toUtf8().constData())));
    const std::string q = xhsapi::queryEncode(kv);
    ensureAuth([this, q, cb](const Result& a) {
        if (a.code != Err::Ok) { cb(a, {}, QString()); return; }
        apiRequest(qFromUtf8("GET").toUtf8(),
                   qFromUtf8("/api/sns/web/v2/comment/page?").append(qsFromStd(q)),
                   QByteArray(),
                   [cb](const Result& r, const QByteArray& b) {
            if (r.code != Err::Ok) { cb(r, {}, QString()); return; }
            const xhsapi::CommentPageLite p = xhsapi::parseComments(
                std::string(b.constData(), (size_t)b.size()));
            QList<CommentItem> out;
            for (size_t i = 0; i < p.items.size(); ++i) {
                CommentItem ci;
                ci.id = qsFromStd(p.items[i].id);
                ci.author = qsFromStd(p.items[i].author);
                ci.text = qsFromStd(p.items[i].content);
                ci.time = p.items[i].createTime;
                out.append(ci);
            }
            cb(Result::ok(), out, qsFromStd(p.cursor));
        });
    });
}

} // namespace qxv
