#pragma once
// 抖音 __ac_signature 双机制对照(2026-11-18)
//   机制 A(权威):acsig.cpp 纯算(CSDN 145899104 还原),结果始终以它为准;
//   机制 B(对照):quickjs-ng 命令行(qjs -C)跑参考 JS
//                (abjs/acsig_runner.js),与 A 同 ts/site/nonce/ua
//                → 理论上字节级一致;
//                不匹配 → qWarning 取证(两侧值+入参);
//                qjs 不存在/超时/异常 → 跳过(仅对照缺席,不阻塞签名)。
// 复用既有开关(不新增环境变量):
//   QXVOTUBE_QJS=qjs 路径(默认 PATH);
//   QXVOTUBE_ABJS_DIR=abjs 目录(默认按 appDir 回推);
//   QXVOTUBE_ABJS_CHECK=0 关闭对照(默认开)。
#include "common/compat.h"

namespace qxv { namespace douyin {

// 机制 B 单独执行:同参数跑参考 JS。返回 false = qjs 不可用/超时/异常
// (对照机制缺席,不视为签名失败)。
bool acSignatureViaJs(long long timeSec, const QString& site,
                      const QString& nonce, const QString& ua, QString* out);

// 主签名入口:算 C++ 结果(权威)并按需对照;返回 C++ 结果。
QString acSignatureChecked(long long timeSec, const QString& site,
                           const QString& nonce, const QString& ua);

}} // namespace qxv::douyin
