#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/streamcache.h"

#include <QMap>

namespace qxv {

// 小红书(2026-11-15 调研见 docs/providers/xiaohongshu-research.md):
//   游客可看 SSR:首页 /explore、笔记详情(需 xsec_token)、用户主页
//   搜索/API 必须登录 → search 返回 Err::NeedAuth
//
// id 形式:
//   - "noteId@xsecToken"(推荐,详情必须 token)
//   - 分享链接(含 ?xsec_token=…)
//   - 裸 noteId:仅当 token 缓存里有(来自 homeFeed/userVideos)才可用
//
// 节流(源码级口径,均值 6s 对数正态,clamp 1.2~30s;触发风控指数冷却):
//   QXVOTUBE_XHS_DELAY_MS / _MAX_PER_MIN / _MAX_PER_DAY / _COOLDOWN_MS
class XiaohongshuProvider : public VideoProvider {
public:
    XiaohongshuProvider();
    QString id() const override { return qFromUtf8("xiaohongshu"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;
    void resolveQuality(const QString& id, const QString& quality,
                        ResolveCb cb) override;
    void userVideos(const QString& uid, int page, SearchCb cb) override;
    void comments(const QString& id, const QString& cursor, CommentsCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override;

private:
    // 节流 + 风控冷却(所有请求都过 gate)
    void gate(std::function<void()> fn);
    void onSoftBlock();          // 软风控:指数冷却
    QString pickToken(const QString& noteId) const;
    void rememberToken(const QString& noteId, const QString& token);

    void fetchPage(const QString& pathWithQuery,
                   std::function<void(const Result&, const QByteArray&)> cb);

    ProxyConfig m_proxy;
    QVariantMap m_cred;
    NetClient m_net;
    CookieJar m_jar;
    bool m_jarInit = false;
    streamcache::Cache m_cache;

    // token 缓存(noteId → token),5 分钟
    struct TokenEntry { QString token; qint64 expiresAt = 0; };
    QMap<QString, TokenEntry> m_tokens;

    // 节流状态
    qint64 m_nextAllowed = 0;
    qint64 m_minuteStart = 0;
    int m_minuteCount = 0;
    qint64 m_dayStart = 0;
    int m_dayCount = 0;
    qint64 m_cooldownUntil = 0;
    int m_blockLevel = 0;   // 0→5min,1→30min,2→120min
    bool m_warmed = false;
};

} // namespace qxv
