#pragma once
#include "compat.h"
#include "result.h"

#include <qvariant.h>

// 认证方式感知的错误模型(2026-11-15):
//   输出错误时必须能区分:cookie / oauth / 用户名密码 / 无凭证(游客)，
//   并附上服务端原始响应(http.status/server.code/server.message)。
//
// 约定:认证类错误统一用 authFail() 构造,写入 Result.detail:
//   auth.mode      "none" | "cookie" | "oauth" | "password"
//   auth.provided  是否已设置凭证(布尔)
//   auth.expired   服务端是否判定失效(布尔)
//   http.status    原始 HTTP 状态(0=未知,不写)
//   server.code    平台原始错误码(如 -101/461/-100),可能为空
//   server.message 平台原始消息(已脱敏:去换行/折叠空白/截断 120 字)
// 凭证明文(cookie/token/password)绝不写入 message/detail。
namespace qxv {

enum class AuthMode { None, Cookie, OAuth, Password };

inline const char* authModeKey(AuthMode m) {
    switch (m) {
    case AuthMode::Cookie:   return "cookie";
    case AuthMode::OAuth:    return "oauth";
    case AuthMode::Password: return "password";
    case AuthMode::None:
    default:                 return "none";
    }
}

struct AuthContext {
    AuthMode mode = AuthMode::None;
    bool provided = false;   // 凭证是否已设置
    bool expired = false;    // 服务端判定失效
};

// 从 provider 通用凭证推断:优先 cookie → token(oauth) → username/password
inline AuthContext credContext(const QVariantMap& cred) {
    AuthContext c;
    if (!qTrim(qMapValue(cred, qFromUtf8("cookie"), QVariant()).toString()).isEmpty()) {
        c.mode = AuthMode::Cookie;
        c.provided = true;
        return c;
    }
    if (!qTrim(qMapValue(cred, qFromUtf8("token"), QVariant()).toString()).isEmpty()) {
        c.mode = AuthMode::OAuth;
        c.provided = true;
        return c;
    }
    if (!qTrim(qMapValue(cred, qFromUtf8("username"), QVariant()).toString()).isEmpty()) {
        c.mode = AuthMode::Password;
        c.provided = true;
    }
    return c;
}

// 脱敏服务器消息(简化空白→含去 CR/LF,超长截断)
inline QString sanitizeServerMsg(const QString& raw, int maxLen = 120) {
    QString s = qSimplified(raw);
    if (s.length() > maxLen) s = s.left(maxLen) + qFromUtf8("…");
    return s;
}

// 统一构造认证/服务端错误(不记录凭证明文)
inline Result authFail(Err code, const AuthContext& ctx, const QString& devMsg,
                       int httpStatus = 0,
                       const QString& serverCode = QString(),
                       const QString& serverMsg = QString(),
                       const QVariantMap& extra = QVariantMap()) {
    Result r = Result::fail(code, devMsg);
    r.detail.insert(qFromUtf8("auth.mode"),
                    QString::fromLatin1(authModeKey(ctx.mode)));
    r.detail.insert(qFromUtf8("auth.provided"), ctx.provided);
    r.detail.insert(qFromUtf8("auth.expired"), ctx.expired);
    if (httpStatus > 0)
        r.detail.insert(qFromUtf8("http.status"), httpStatus);
    if (!serverCode.isEmpty())
        r.detail.insert(qFromUtf8("server.code"), serverCode);
    if (!serverMsg.isEmpty())
        r.detail.insert(qFromUtf8("server.message"), sanitizeServerMsg(serverMsg));
    for (QVariantMap::const_iterator it = extra.begin(); it != extra.end(); ++it)
        r.detail.insert(it.key(), *it);
    return r;
}

// 非认证类错误:只附加服务端原始信息(不写 auth.*)
inline Result withServer(Result r, int httpStatus,
                         const QString& serverCode = QString(),
                         const QString& serverMsg = QString()) {
    if (httpStatus > 0) r.detail.insert(qFromUtf8("http.status"), httpStatus);
    if (!serverCode.isEmpty())
        r.detail.insert(qFromUtf8("server.code"), serverCode);
    if (!serverMsg.isEmpty())
        r.detail.insert(qFromUtf8("server.message"), sanitizeServerMsg(serverMsg));
    return r;
}

inline AuthMode authModeOf(const Result& r) {
    const QString m = qMapValue(r.detail, qFromUtf8("auth.mode"), QVariant()).toString();
    if (m == qFromUtf8("cookie")) return AuthMode::Cookie;
    if (m == qFromUtf8("oauth")) return AuthMode::OAuth;
    if (m == qFromUtf8("password")) return AuthMode::Password;
    return AuthMode::None;
}

} // namespace qxv
