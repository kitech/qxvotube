#ifndef API_H
#define API_H

#include "eventpoller.h"

// 结果事件：复用 eventpoller.h 既有 ApiResultReadyType/ApiResultEvent
// （直接引用 qldox 原文件，契约随头文件进入），仅补带原始 HttpResponse
class ApiHttpResultEvent : public ApiResultEvent {
public:
    HttpResponse resp;
    void* user;
    ApiHttpResultEvent(ApiRequestType t, const HttpResponse& r, void* u)
        : ApiResultEvent(t), resp(r), user(u) {}
};

// 最小 API 门面（照 qldox restapi 模式：request/onHttpDone/postEvent 回投，不带业务端点）
class Api {
public:
    static void setEventTarget(QObject* target);
    static void setBaseUrl(const std::string& url);
    // url 以 '/' 开头且已设 baseUrl 时拼接；否则原样使用
    static void request(const HttpRequest& req, ApiRequestType type, void* user = 0);

private:
    static void onHttpDone(const HttpResponse& resp, void* udata);
};

#endif // API_H
