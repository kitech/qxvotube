#include "ssr.h"

namespace qxv {
namespace xiaohongshu {
namespace ssr {

bool extractInitialState(const QByteArray& html, JRoot& out) {
    static const QByteArray kVar("__INITIAL_STATE__");
    const int at = html.indexOf(kVar);
    if (at < 0) return false;
    const int brace = html.indexOf('{', at + kVar.size());
    if (brace < 0) return false;
    int end = html.indexOf("</script>", brace);
    if (end < 0) end = html.size();
    QByteArray body = html.mid(brace, end - brace);
    // JS 里的裸 undefined 不是合法 JSON;替换为 null(长度不变,偏移安全)
    body.replace("undefined", "null     ");
    out.p = cJSON_ParseWithLengthOpts(body.constData(),
                                      static_cast<size_t>(body.size()),
                                      nullptr, 0);
    return out.ok();
}

} // namespace ssr
} // namespace xiaohongshu
} // namespace qxv
