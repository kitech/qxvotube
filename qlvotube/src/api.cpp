#include "api.h"
#include <qapplication.h>

static QObject* s_target = 0;
static std::string s_baseUrl;

namespace {
struct ApiCallCtx {
    ApiRequestType type;
    void* user;
};
}

void Api::setEventTarget(QObject* target) { s_target = target; }
void Api::setBaseUrl(const std::string& url) { s_baseUrl = url; }

void Api::request(const HttpRequest& req, ApiRequestType type, void* user)
{
    HttpRequest r = req;
    if (!s_baseUrl.empty() && !r.url.empty() && r.url[0] == '/') {
        r.url = s_baseUrl + r.url;
    }
    ApiCallCtx* ctx = new ApiCallCtx();
    ctx->type = type;
    ctx->user = user;
    EventPoller::addRequest(r, onHttpDone, ctx);
}

// 泵线程回调 → 投递回 UI 线程（照 qldox restapi.cpp:210-224）
void Api::onHttpDone(const HttpResponse& resp, void* udata)
{
    ApiCallCtx* ctx = static_cast<ApiCallCtx*>(udata);
    if (s_target) {
        QApplication::postEvent(s_target, new ApiHttpResultEvent(ctx->type, resp, ctx->user));
    }
    delete ctx;
}
