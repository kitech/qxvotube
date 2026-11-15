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
    QString id() const override { return qFromUtf8("youtube"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override {}

private:
    // visitorId:X-Goog-Visitor-Id 头(与 body 里 client.visitorData 同值)。
    // 实测:缺此头时 search/player 一律 backgroundPromoRenderer(空结果/需登录),
    // 有它 + Origin/Referer 即正常(WEB 游客态)。
    void innertube(const QString& endpoint, const QByteArray& body,
                   const char* ua,
                   std::function<void(const NetResponse&)> cb,
                   const QString& visitorId = QString(),
                   bool noCookie = false);
    ProxyConfig m_proxy;
    QVariantMap m_cred;
    // 首页预热时抓到的 visitorData(访客身份;缺失时 search/player 一律空结果/需登录)
    QString m_visitorData;
};

} // namespace qxv
