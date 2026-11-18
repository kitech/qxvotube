#pragma once
// 抖音 a_bogus 双机制对照(2026-11-18)
//   机制 A(权威):abogus.cpp 纯 C++ 移植,结果始终以它为准;
//   机制 B(对照):quickjs-ng 命令行(qjs -C)跑参考原版 JS(abjs/runner.js),
//                与 A 同 seed/now/uri/ua/窗口指纹 → 理论上字节级一致;
//                不匹配时 qWarning 输出两侧值;qjs 不存在/超时/异常 → 跳过
//                (仅警告,不阻塞签名,不影响主流程)。
// 环境开关:QXVOTUBE_QJS=qjs 路径(默认 PATH 里找);
//           QXVOTUBE_ABJS_DIR=abjs 目录(默认按 appDir 回推);
//           QXVOTUBE_ABJS_CHECK=0 关闭对照(默认开)。
#include <string>

namespace qxv { namespace abogus {

// 机制 B 单独执行:同参数跑参考 JS,输出 a_bogus。
// 返回 false = qjs 不可用/超时/异常(对照机制缺席,不视为签名失败)。
bool generateViaJs(unsigned int seed, long long nowMs,
                   const std::string& uri, const std::string& ua,
                   const char* windowFp, std::string* out);

// 主签名入口:算 C++ 结果(权威)并按需对照;返回 C++ 结果。
std::string generateChecked(const std::string& uri, const std::string& ua,
                            long long nowMs, unsigned int seed,
                            const char* windowFp = 0);

}} // namespace qxv::abogus
