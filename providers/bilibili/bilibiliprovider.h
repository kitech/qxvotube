#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"

namespace qxv {

// bilibili:REST 接口(web 端游客可用口)
// 资料:bilibili-API-collect(search/x/web-interface, playurl v1)
// 认证:search/playurl 游客可用(低清晰度);高清/番剧需登录 → NeedAuth
class BilibiliProvider : public VideoProvider {
public:
    QString id() const override { return qFromUtf8("bilibili"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override {}

private:
    void apiGet(const QString& path, const QString& query,
                std::function<void(const NetResponse&)> cb);
    ProxyConfig m_proxy;
    QVariantMap m_cred;
};

} // namespace qxv
