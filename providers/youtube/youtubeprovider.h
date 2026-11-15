#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"

namespace qxv {

// YouTube:InnerTube /youtubei/v1/player(游客,免 API key/PoToken)
// 资料:yt-dlp INNERTUBE_CLIENTS、pytubefix;ANDRIOD/IOS client 校验最松
// 认证:游客可播(非年龄限制);年龄限制 → NeedAuth;地区锁 → GeoBlocked
// ★ 网络:YouTube 需代理——setProxy() 或环境变量 QXVOTUBE_PROXY=http://host:port
//   (代码内置该环境变量口;测试口径:http://127.0.0.1:10809;代理慢,验证视频只拉开头)
class YoutubeProvider : public VideoProvider {
public:
    QString id() const override { return QXV_STR("youtube"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override {}

private:
    void innertube(const QString& endpoint, const QByteArray& body,
                   std::function<void(const NetResponse&)> cb);
    ProxyConfig m_proxy;
    QVariantMap m_cred;
};

} // namespace qxv
