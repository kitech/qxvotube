#pragma once
#include "compat.h"
#include <QObject>
#include <QVector>
#include <functional>

// 统一 HTTP 客户端:cpp-httplib(vendored)+ QObject 薄封装
// - 阻塞调用在 QThreadPool 里跑,回调经队列投递回调用方所在线程
// - 所有 Qt 版本共用同一实现(不依赖 QNetworkAccessManager)
// - 自管 CookieJar,不依赖 QNetworkCookieJar
// 调试:dumpHttp 开 Info 时请求/响应脱敏写日志

namespace qxv {

struct NetRequest {
    QString method = qFromUtf8("GET");
    QString url;
    QVector<QPair<QString, QString>> headers;   // 不用 Qt6 QHttpHeaders(兼容低版)
    QByteArray body;
    QString proxy;                              // "http://host:port" 空=直连
    int timeoutSec = 15;
    bool autoCookie = true;                     // CookieJar 自动附带/回存
};

struct NetResponse {
    int status = 0;
    QVector<QPair<QString, QString>> headers;
    QByteArray body;
    QString error;                              // httplib 侧错误(空=成功)
    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

class CookieJar;

class NetClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const NetResponse&)>;

    explicit NetClient(QObject* parent = nullptr);
    ~NetClient() override;

    void setCookieJar(CookieJar* jar);          // 所有权留在调用方(可共享)
    void send(const NetRequest& req, Callback cb);   // 立即返回;回调在发起线程
    void cancel();                              // 作废在途结果(P2 扩展标记丢弃)

    static QString cookieHeaderFrom(const NetResponse&);   // Set-Cookie 便捷取值

private:
    struct Impl;
    Impl* d;
};

} // namespace qxv
