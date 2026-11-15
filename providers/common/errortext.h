#pragma once
#include "compat.h"
#include "result.h"
#include <QString>

// 错误码 → 用户可读文案(i18n key,复用 qlcomp Translator)
// 对照表约定:每个 Err 枚举值在 lang/*.json 都有 error.* 对应 key(tests/errortext.test 锁定)
namespace qxv {

QString i18nKeyFor(Err code);   // 如 Err::Network → "error.network"

// 认证类错误优先按 auth.mode + auth.expired 选键(比单看 Err 更精确):
//   NeedAuth + 未提供凭证     → error.auth_guest_need_login
//   NeedAuth + 已提供 cookie  → error.auth_cookie_missing
//   AuthExpired + cookie/未知 → error.auth_cookie_expired
//   AuthExpired + oauth       → error.auth_oauth_expired
//   AuthExpired + password    → error.auth_password_failed
QString i18nKeyFor(const Result& r);

} // namespace qxv
