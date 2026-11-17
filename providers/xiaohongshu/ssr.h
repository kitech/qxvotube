#pragma once
#include "common/compat.h"
#include "common/qcjson.h"

// 小红书 SSR 解析:window.__INITIAL_STATE__ = {...}
// 与红果 _ROUTER_DATA 的差异:
//   - 变量名是 __INITIAL_STATE__(不是 _ROUTER_DATA)
//   - JS 里含裸 undefined,直接 cJSON 解析会失败 → 先替换成 null
//   - 结尾以 </script> 截断(不能吃到整页)
namespace qxv {
namespace xiaohongshu {
namespace ssr {

bool extractInitialState(const QByteArray& html, JRoot& out);

} // namespace ssr
} // namespace xiaohongshu
} // namespace qxv
