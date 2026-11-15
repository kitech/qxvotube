#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"

namespace qxv {

// 抖音(2026-11-15 侦察结论,见 docs/providers/protocols/bytedance.md §13):
//   ✅ 热榜免签:GET douyin.com/aweme/v1/web/hot/search/list/ → data.word_list[]
//   ❌ search / aweme detail / resolve 需要 a_bogus(web)或 Argus/uifid 签名,
//      实测 200+空 data 或 "Blocked by ArgusSecurityPlugin"。
// 因此本 provider 只提供 homeFeed(热榜/热词),其余能力显式 Err::Unsupported,
// 不返回假数据、不静默失败。
class DouyinProvider : public VideoProvider {
public:
    QString id() const override { return qFromUtf8("douyin"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override;

private:
    ProxyConfig m_proxy;
    QVariantMap m_cred;
    NetClient m_net;
    CookieJar m_jar;
    bool m_jarInit = false;
};

} // namespace qxv
