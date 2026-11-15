#pragma once
#include "compat.h"
#include "result.h"
#include <QString>

// 错误码 → 用户可读文案(i18n key,复用 qlcomp Translator)
// 对照表约定:每个 Err 枚举值在 lang/*.json 都有 error.* 对应 key(tests/errortext.test 锁定)
namespace qxv {

QString i18nKeyFor(Err code);   // 如 Err::Network → "error.network"

} // namespace qxv
