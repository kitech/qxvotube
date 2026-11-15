#pragma once
#include "common/provider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/streamcache.h"

namespace qxv {

// 红果短剧(通道①官网公开口,无签名/无设备注册/直连):
//   搜索  GET https://hongguoduanju.com/api/search/result?query=
//   详情  GET https://hongguoduanju.com/detail?series_id=   (HTML 内嵌 _ROUTER_DATA)
//   取流  POST /api/video/protected/fallback-play-info {"item_id":<vid>}
//         → 明文渐进 mp4(360P);试看集放行(accessible_episode_cnt),
//           VIP 集 403 public_fallback_unavailable → Paywalled
// 高清档(通道②App 协议,零签名 H3a):multi_video_model/v1 游客口径 →
//   1080p(bytevc1/HEVC) CENC 密文 + spade_a → hgstream 本地流式解密代理
//   → player 连 http://127.0.0.1:<port>/hg/<token>.mp4 播明文。
// resolve 顺序:App 1080p(bytevc1) 优先 → web 360p 明文保底
//   (App 链失败/只有 bytevc2 私有编码/代理起不来均回退,不静默失败)。
class HongguoProvider : public VideoProvider {
public:
    HongguoProvider() = default;
    ~HongguoProvider() override;   // 释放当前本地代理会话

    QString id() const override { return qFromUtf8("hongguo"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;
    void resolveQuality(const QString& id, const QString& quality,
                        ResolveCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override;

private:
    void webGet(const QString& path, const QString& query,
                std::function<void(const NetResponse&)> cb);
    void webPostJson(const QString& path, const QByteArray& body,
                     const QString& referer,
                     std::function<void(const NetResponse&)> cb);
    ProxyConfig m_proxy;
    QVariantMap m_cred;
    NetClient m_net;
    CookieJar m_jar;
    bool m_jarInit = false;
    streamcache::Cache m_cache;
    // 当前本地代理会话(切集/析构时 release,避免会话泄漏)
    QString m_streamVid;
    QString m_streamQuality;
};

} // namespace qxv
