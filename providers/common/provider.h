#pragma once
#include "compat.h"
#include "result.h"
#include "debuglog.h"

namespace qxv {

struct ProxyConfig {
    bool enabled = false;
    enum Type { Http, Socks5 } type = Http;
    QString host, user, pass;
    int port = 0;
    bool stickySession = false;   // TikTok/抖音:注册与请求同 IP
};

// ProxyConfig → libcurl 代理 URL。disabled/缺 host/port 返回空(=直连)。
// 按 type 选 http:// 或 socks5://,带 user:pass@ 认证。Qt3 兼容纯 QString。
inline QString proxyUrl(const ProxyConfig& p)
{
    if (!p.enabled || p.host.isEmpty() || p.port <= 0) return QString();
    const QString scheme = (p.type == ProxyConfig::Socks5)
        ? qFromUtf8("socks5://") : qFromUtf8("http://");
    QString auth;
    if (!p.user.isEmpty()) {
        auth = p.user;
        if (!p.pass.isEmpty()) auth += qFromUtf8(":") + p.pass;
        auth += qFromUtf8("@");
    }
    return scheme + auth + p.host + qFromUtf8(":") + QString::number(p.port);
}

// 墙外站点清单(必须代理)。集中定义，新增站点只需在此加 id：
//   youtube / tiktok / snapchat / instagram —— 无代理直连通常不可达；
//   但 VPN 环境下系统级已代理、没有显式 proxy 也能连通，故缺显式代理时警告+直连。
// bilibili/douyin/hongguo/xiaohongshu 等国内站点不在清单 → 恒直连(避免代理 IP 触发风控)。
inline bool siteMustProxy(const QString& id)
{
    return id == qFromUtf8("youtube") || id == qFromUtf8("tiktok")
        || id == qFromUtf8("snapchat") || id == qFromUtf8("instagram");
}

// 站点代理策略：mustProxy 决定「是否走代理」，全局代理配置决定「代理地址」。
// 优先级：QXVOTUBE_PROXY 环境变量(普遍适用、最高) > 设置页/显式 ProxyConfig。
// 叠加语义：
//   - mustProxy=false：恒直连（国内站点，避免代理 IP 触发风控）。
//   - mustProxy=true 且有显式代理：走代理。
//   - mustProxy=true 但无显式代理：警告 + 直连。VPN 场景系统级已代理也能连通，
//     不能因缺显式 proxy 就硬失败；直连让 curl 走系统路由，交给实际网络判定。
inline QString effectiveProxy(const ProxyConfig& p, bool mustProxy, const char* mod)
{
    if (!mustProxy) return QString();
    const QString envPx = qenv("QXVOTUBE_PROXY");          // 环境变量优先级最高
    if (!envPx.isEmpty()) return envPx;
    if (p.enabled && !p.host.isEmpty() && p.port > 0) return proxyUrl(p);
    DBG_P(mod, Warn, "must_proxy site: no explicit proxy -> direct (VPN/system route may work)");
    return QString();                                       // 警告后直连，信任 VPN
}

// 站点分类(UI chips)。key 原样回传 categoryFeed(),UI 不解释语义。
// 空 key 约定为「推荐」(走 homeFeed)。
struct CategoryItem {
    QString key, name;
    CategoryItem() {}
    CategoryItem(const QString& k, const QString& n) : key(k), name(n) {}
};

// 认证级别(文档 providers/docs/protocols/*.md 与接口一一对应)
enum AuthLevel { NoAuth, DeviceAuth, CookieAuth, FullLogin };

class VideoProvider {
public:
    virtual ~VideoProvider() = default;
    virtual QString id() const = 0;

    // ── 发现(游客即可) ──
    virtual void search(const QString& kw, int page, SearchCb) = 0;
    virtual void homeFeed(int page, SearchCb cb) {
        cb(Result::fail(Err::Unsupported, "homeFeed not implemented"), {}, false);
    }

    // 本站分类列表。动态拉取(从站点真实数据派生),不返回硬编码分类表;
    // 无此能力/拉取失败 → Unsupported,UI 自动退化为仅「推荐」一个 chip。
    virtual void categories(
        std::function<void(const Result&, const QList36<CategoryItem>&)> cb) {
        cb(Result::fail(Err::Unsupported, "categories not implemented"), {});
    }
    // 按分类取列表(key 来自 categories();空 key = 推荐)
    virtual void categoryFeed(const QString& key, int page, SearchCb cb) {
        Q_UNUSED(key);
        homeFeed(page, cb);
    }

    // ── 详情与播放 ──
    virtual void detail(const QString& id, DetailCb) = 0;
    virtual void resolve(const QString& id, ResolveCb) = 0;
    // 清晰度选择:quality 空 = provider 默认(等价 resolve)。
    // 口径统一 "360p"/"480p"/"540p"/"720p"/"1080p"(大小写不敏感);
    // 不可用档位:能降级则在 Stream.extra 标 requestedQuality+degraded=true;
    // 完全不可播则 Err::Unsupported/NotFound(不静默给错档)。
    virtual void resolveQuality(const QString& id, const QString& quality,
                                ResolveCb cb) {
        Q_UNUSED(quality);
        resolve(id, std::move(cb));
    }

    // ── 互动(缺省 Unsupported,返回 Result 不静默) ──
    virtual void comments(const QString& id, const QString& cursor, CommentsCb cb) {
        Q_UNUSED(id); Q_UNUSED(cursor);
        cb(Result::fail(Err::Unsupported, "comments not implemented"), {}, QString());
    }
    virtual void userVideos(const QString& uid, int page, SearchCb cb) {
        Q_UNUSED(uid); Q_UNUSED(page);
        cb(Result::fail(Err::Unsupported, "userVideos not implemented"), {}, false);
    }

    // ── 配置 ──
    // 站点是否墙外必需代理：按 id 自动判定(见 siteMustProxy)。默认实现，个别站点可覆写。
    virtual bool mustProxy() const { return siteMustProxy(id()); }
    virtual void setProxy(const ProxyConfig&) = 0;
    virtual void setCredentials(const QVariantMap&) = 0;  // 不设=游客
    virtual void cancel() = 0;
};

// 注册表:平台 id → provider 实例(main/UI 层据此枚举源)
QList36<VideoProvider*> createProviders();

} // namespace qxv
