#include "compat.h"
#include "errortext.h"

namespace qxv {

QString i18nKeyFor(const Result& r) {
    if (r.code == Err::NeedAuth || r.code == Err::AuthExpired) {
        const QString mode = qMapValue(r.detail, qFromUtf8("auth.mode"), QVariant()).toString();
        const bool provided = qMapValue(r.detail, qFromUtf8("auth.provided"), QVariant()).toBool();
        if (r.code == Err::AuthExpired) {
            if (mode == qFromUtf8("oauth"))
                return qFromUtf8("error.auth_oauth_expired");
            if (mode == qFromUtf8("password"))
                return qFromUtf8("error.auth_password_failed");
            return qFromUtf8("error.auth_cookie_expired");
        }
        if (!provided) return qFromUtf8("error.auth_guest_need_login");
        if (mode == qFromUtf8("cookie"))
            return qFromUtf8("error.auth_cookie_missing");
        if (mode == qFromUtf8("oauth"))
            return qFromUtf8("error.auth_oauth_expired");
        if (mode == qFromUtf8("password"))
            return qFromUtf8("error.auth_password_failed");
    }
    return i18nKeyFor(r.code);
}

QString i18nKeyFor(Err code) {
    switch (code) {
    case Err::Ok:             return qFromUtf8("");
    case Err::Network:        return qFromUtf8("error.network");
    case Err::Tls:            return qFromUtf8("error.tls");
    case Err::Http:           return qFromUtf8("error.http");
    case Err::Parse:          return qFromUtf8("error.parse");
    case Err::RateLimit:      return qFromUtf8("error.ratelimit");
    case Err::CaptchaRequired:return qFromUtf8("error.captcha");
    case Err::RiskHint:       return qFromUtf8("error.risk");
    case Err::NotFound:       return qFromUtf8("error.notfound");
    case Err::NeedAuth:       return qFromUtf8("error.needauth");
    case Err::AuthExpired:    return qFromUtf8("error.authexpired");
    case Err::Paywalled:      return qFromUtf8("error.paywalled");
    case Err::GeoBlocked:     return qFromUtf8("error.geoblocked");
    case Err::Unsupported:    return qFromUtf8("error.unsupported");
    case Err::Cancelled:      return qFromUtf8("");
    }
    return qFromUtf8("error.http");
}

} // namespace qxv
