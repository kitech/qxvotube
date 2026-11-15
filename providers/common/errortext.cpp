#include "compat.h"
#include "errortext.h"

namespace qxv {

QString i18nKeyFor(Err code) {
    switch (code) {
    case Err::Ok:             return QXV_STR("");
    case Err::Network:        return QXV_STR("error.network");
    case Err::Tls:            return QXV_STR("error.tls");
    case Err::Http:           return QXV_STR("error.http");
    case Err::Parse:          return QXV_STR("error.parse");
    case Err::RateLimit:      return QXV_STR("error.ratelimit");
    case Err::CaptchaRequired:return QXV_STR("error.captcha");
    case Err::RiskHint:       return QXV_STR("error.risk");
    case Err::NotFound:       return QXV_STR("error.notfound");
    case Err::NeedAuth:       return QXV_STR("error.needauth");
    case Err::AuthExpired:    return QXV_STR("error.authexpired");
    case Err::Paywalled:      return QXV_STR("error.paywalled");
    case Err::GeoBlocked:     return QXV_STR("error.geoblocked");
    case Err::Unsupported:    return QXV_STR("error.unsupported");
    case Err::Cancelled:      return QXV_STR("");
    }
    return QXV_STR("error.http");
}

} // namespace qxv
