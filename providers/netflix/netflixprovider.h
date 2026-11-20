#pragma once
#include "common/provider.h"
#include "common/netclient.h"

namespace qxv {

// Netflix(unogsng 目录元数据 + 本地静态数据集兜底):
//   - 不接 Netflix 站内 API(需登录 + DRM),只做"目录展示",不提供播放流
//   - 在线:uNoGS(RapidAPI)/genres、/search、/title
//     key 来自环境变量 QXVOTUBE_UNOGS_KEY(可选,无 key 自动走本地)
//   - 离线:本地数据集(编译进二进制),覆盖 genres/search/detail
//   - resolve/resolveQuality 固定 Err::Unsupported(无免登录播放源)
class NetflixProvider : public VideoProvider {
public:
    QString id() const override { return qFromUtf8("netflix"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void categories(std::function<void(const Result&,
                                       const QList36<CategoryItem>&)>) override;
    void categoryFeed(const QString& key, int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;
    void resolveQuality(const QString& id, const QString& quality,
                        ResolveCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap&) override {}  // 无登录态,key 走环境变量
    void cancel() override { m_net.cancel(); }

private:
    bool hasKey() const;
    void unogGet(const QString& path, const QString& query,
                 std::function<void(const NetResponse&)> cb);

    ProxyConfig m_proxy;
    NetClient m_net;
};

} // namespace qxv
