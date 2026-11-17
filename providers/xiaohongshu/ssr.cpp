#include "ssr.h"
#include <cstring>

namespace qxv {
namespace xiaohongshu {
namespace ssr {

bool extractInitialState(const QByteArray& html, JRoot& out) {
    static const QByteArray kVar = qbaFromCharptr("__INITIAL_STATE__");
    const int at = qIndexOf(html, kVar);
    if (at < 0) return false;
    const int brace = qIndexOf(html, qFromUtf8("{"), at + (int)kVar.size());
    if (brace < 0) return false;
    int end = qIndexOf(html, qFromUtf8("</script>"), brace);
    if (end < 0) end = (int)html.size();
    QByteArray body = qbaMid(html, brace, end - brace);
    // JS 里的裸 undefined 不是合法 JSON;等长替换为 null(偏移安全)。
    // Qt3 QByteArray 无 replace(),直接在缓冲区上做等长替换。
    {
        const char* from = "undefined";
        const char* to   = "null     ";
        const int flen = 9;
        const int n = (int)body.size();
        char* p = body.data();
        for (int i = 0; i + flen <= n; ) {
            if (std::memcmp(p + i, from, (size_t)flen) == 0) {
                std::memcpy(p + i, to, (size_t)flen);
                i += flen;
            } else {
                ++i;
            }
        }
    }
    out.p = cJSON_ParseWithLengthOpts(body.data(),
                                      static_cast<size_t>(body.size()),
                                      nullptr, 0);
    return out.ok();
}

} // namespace ssr
} // namespace xiaohongshu
} // namespace qxv
