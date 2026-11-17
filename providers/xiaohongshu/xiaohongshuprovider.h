#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/streamcache.h"

#include <qmap.h>

#include <string>

namespace qxv {

namespace xhssign { class Signer; }   // 纯 C++ 签名模块(前置声明)

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
    ~XiaohongshuProvider() override;
    QString id() const override { return qFromUtf8("xiaohongshu"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void categories(std::function<void(const Result&,
                                       const QList36<CategoryItem>&)>) override;
    void categoryFeed(const QString& key, int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;
    void resolveQuality(const QString& id, const QString& quality,
                        ResolveCb cb) override;
    void userVideos(const QString& uid, int page, SearchCb cb) override;
    void comments(const QString& id, const QString& cursor, CommentsCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override;
    void cancel() override;

private:
    // 节流 + 风控冷却(所有请求都过 gate)
    void gate(std::function<void()> fn);
    void onSoftBlock();          // 软风控:指数冷却
    QString pickToken(const QString& noteId) const;
    void rememberToken(const QString& noteId, const QString& token);

    void fetchPage(const QString& pathWithQuery,
                   std::function<void(const Result&, const QByteArray&)> cb);

    // ── 登录态 API(2026-11-17)───────────────────────────────
    // 凭证:cookie 需含 web_session + a1(缺一 → hasSession()==false)
    bool hasSession() const;
    std::string cookieStr() const;
    // 带 x-s / x-s-common 的 API 请求(POST 用紧凑 JSON body)
    void apiRequest(const QByteArray& method, const QString& path,
                    const QByteArray& body,
                    std::function<void(const Result&, const QByteArray&)> cb);
    // 探测登录态(成功缓存 5 分钟;-101/401 → AuthExpired)
    void ensureAuth(std::function<void(const Result&)> cb);
    // 服务端信号 → Result(区分认证/风控/其它,附原始响应)
    Result mapApiFailure(int httpStatus, const QByteArray& body);

    // 登录态 feed 兜底:POST /api/sns/web/v1/feed → PlayResult
    // (SSR 拿不到详情/流时使用;失败透传 Result)
    void resolveByApi(const QString& noteId, const QString& token,
                      std::function<void(const Result&, const PlayResult*)> cb);

    // userVideos 的游标(登录态分页;空 = 首页)
    QString m_userCursor;

    ProxyConfig m_proxy;
    QVariantMap m_cred;
    NetClient m_net;
    CookieJar m_jar;
    bool m_jarInit = false;
    streamcache::Cache m_cache;

    // 登录态签名器 + 探测缓存 + dry-run
    xhssign::Signer* m_signer = nullptr;   // lazy,析构释放
    bool m_authOk = false;
    qint64 m_authCheckedAt = 0;
    bool m_dryRun = false;                 // QXVOTUBE_XHS_DRYRUN=1

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
