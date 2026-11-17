#pragma once
#include "compat.h"
#include <qobject.h>
#include <functional>
#include <memory>

// 统一 HTTP 客户端 —— 传输层 = qlappcom EventPoller(curl multi,自带泵线程)
// - 不新增线程:请求交给泵线程,响应经 qAppPostEvent 回到发起线程
// - 保留:Cookie / 重试退避 / 限流 / 单飞(请求去重) / cancel 代际
// - Qt3/Qt4/Qt6 同码;不再依赖 httplib
namespace qxv {

struct NetRequest {
    QString method = qFromUtf8("GET");
    QString url;
    NetHeaders headers;                 // 值容器别名(Qt3: QValueVector)
    QByteArray body;
    QString proxy;                      // "http://host:port" / "socks5://..."
    int timeoutSec = 15;
    int stallSec = 45;                  // 无进展看门狗
    bool autoCookie = true;
    bool singleFlight = true;
    int maxAttempts = 1;                // 1 = 不重试
    int retryDelayMs = 250;
    bool retryOnEmpty = false;          // 字节系"200 空包"可开
};

struct NetResponse {
    int status = 0;
    NetHeaders headers;                 // 多值已由引擎按 '\n' 拼接
    QByteArray body;
    QString error;                      // curl 侧错误(空=成功)
    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

class CookieJar;

class NetClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const NetResponse&)>;
    struct Impl;

    explicit NetClient(QObject* parent = 0);
    ~NetClient() override;

    void setCookieJar(CookieJar* jar);          // 所有权留在调用方(可共享)
    void send(const NetRequest& req, Callback cb);   // 立即返回;回调在发起线程
    void cancel();                              // 本客户端在途结果作废

    static QString cookieHeaderFrom(const NetResponse& r);

protected:
    void customEvent(CustomEventBase* e) override;   // 泵线程事件落地

private:
    void dispatch(const NetRequest& req, int gen, const QString& key, int attempt);
    std::shared_ptr<Impl> d;
};

} // namespace qxv
