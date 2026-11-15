#pragma once
// providers 跨 Qt 版本兼容宏(目标:Qt3/4/6 同一份源码语义;providers 库本身
// 仅在 Qt>=5 构建编译,Qt3 引用方走 stub,见 qxv_compat_guard.h)
#include <QtGlobal>

// ── 字符串字面量 ──────────────────────────────────────────────
// Qt>=5.2:QStringLiteral(编译期 UTF-16);更低:fromUtf8(带长度,免 strlen)
// 注意:项目口径"字符串全用 utf8",禁用 QLatin1String/QLatin1Char
#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
#define QXV_STR(s) QStringLiteral(s)
#else
#define QXV_STR_QLEN(s) "" s "", sizeof(s) - 1
#define QXV_STR(s) QString::fromUtf8("" s "", static_cast<int>(sizeof(s) - 1))
#endif

// ── split 空段处理 ────────────────────────────────────────────
// Qt<5.14 挂在 QString 命名空间下
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define QXV_SKIP_EMPTY Qt::SkipEmptyParts
#else
#define QXV_SKIP_EMPTY QString::SkipEmptyParts
#endif

// ── Qt::CaseInsensitive 比较(Qt3 无重载)──────────────────────
// 用法:两串各自 .toUpper() 后 ==,统一走 QXV_IEQ(a, b)
#define QXV_IEQ(a, b) ((a).toUpper() == (b).toUpper())

// ── qEnvironmentVariable(Qt>=5.10;更低走 getenv)─────────────
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
#define QXV_ENV(var) qEnvironmentVariable(var)
#define QXV_ENV_EMPTY(var) qEnvironmentVariableIsEmpty(var)
#else
#include <cstdlib>
inline QString qxv_env(const char* var) {
    const char* v = std::getenv(var);
    return v ? QString::fromUtf8(v) : QString();
}
inline bool qxv_env_empty(const char* var) {
    const char* v = std::getenv(var);
    return v == nullptr || v[0] == '\0';
}
#define QXV_ENV(var) qxv_env(var)
#define QXV_ENV_EMPTY(var) qxv_env_empty(var)
#endif
