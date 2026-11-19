#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"

namespace qxv {

// 抖音(2026-11-15 侦察结论,见 docs/providers/protocols/bytedance.md §13):
//   ✅ 热榜免签:GET douyin.com/aweme/v1/web/hot/search/list/ → data.word_list[]
// 抖音(侦察见 docs/providers/protocols/bytedance.md §13,签名见 §15):
//   ✅ 热榜免签:GET douyin.com/aweme/v1/web/hot/search/list/ → data.word_list[]
//   ✅ search/detail/resolve 走 a_bogus 签名(2026-11-18 落码,
//      generateChecked 双机制对照);需 ttwid cookie,首次预热获取。
// 失败一律显式 Err(风控/改版→RateLimit/Parse),不返回假数据、不静默失败。
class DouyinProvider : public VideoProvider {
public:
    QString id() const override { return qFromUtf8("douyin"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;
    void resolveQuality(const QString& id, const QString& quality,
                        ResolveCb cb) override;
    // 字幕空壳:抖音无公开字幕接口(硬字幕烧录在画面,需 ASR/OCR) → 显式不支持
    void subtitles(const QString& id, SubtitlesCb cb) override {
        Q_UNUSED(id);
        cb(Result::fail(Err::Unsupported,
            qFromUtf8("抖音无公开字幕接口")), QList36<SubtitleTrack>());
    }

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override;

private:
    // 首次签名请求前预热:首页 nonce → 纯算 __ac_signature 二跳
    // → ttwid/UIFID_TEMP;失败回退 union register。只预热一次。
    void ensureTtwid(std::function<void()> done);
    // 回退:ttwid.bytedance.com union register(Set-Cookie→callback 落 .douyin.com)
    void registerTtwid(std::function<void()> done);
    // detail 取数统一口:5 分钟单条缓存(detail→resolve 连击不重打)
    void fetchDetail(const QString& id,
                     std::function<void(const Result&, const QByteArray&)> cb);

    ProxyConfig m_proxy;
    QVariantMap m_cred;
    NetClient m_net;
    CookieJar m_jar;
    bool m_jarInit = false;
    bool m_ttwidWarm = false;
    bool m_credImported = false;   // credentials["cookie"] 已注入 jar
    QString m_cacheId;           // detail 单条缓存
    QByteArray m_cacheBody;
    long long m_cacheAt = 0;
};

} // namespace qxv
