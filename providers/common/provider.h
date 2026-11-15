#pragma once
#include "compat.h"
#include "result.h"

namespace qxv {

struct ProxyConfig {
    bool enabled = false;
    enum Type { Http, Socks5 } type = Http;
    QString host, user, pass;
    int port = 0;
    bool stickySession = false;   // TikTok/抖音:注册与请求同 IP
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

    // ── 详情与播放 ──
    virtual void detail(const QString& id, DetailCb) = 0;
    virtual void resolve(const QString& id, ResolveCb) = 0;

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
    virtual void setProxy(const ProxyConfig&) = 0;
    virtual void setCredentials(const QVariantMap&) = 0;  // 不设=游客
    virtual void cancel() = 0;
};

// 注册表:平台 id → provider 实例(main/UI 层据此枚举源)
QList<VideoProvider*> createProviders();

} // namespace qxv
