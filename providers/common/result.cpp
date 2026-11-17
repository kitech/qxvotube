#include "compat.h"
#include "result.h"

namespace qxv {

bool Result::retriable() const {
    switch (code) {
    case Err::Network:
    case Err::RateLimit:
        return true;
    case Err::Http: {
        const int st = qMapValue(detail, qFromUtf8("httpStatus"), QVariant()).toInt();
        return st >= 500 && st < 600;
    }
    default:
        return false;
    }
}

} // namespace qxv
