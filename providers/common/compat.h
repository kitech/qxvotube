#pragma once
#include "compatcore34.h"   // qlcomp:qFromUtf8/qbaFromCharptr 等(字符串全 UTF-8 口径)
#include "qt3shim.h"        // Qt3:QVariantMap 等缺失类型补齐(实现在 Qt>=5)
// providers 跨 Qt 版本兼容宏(目标:Qt3/4/6 同一份源码语义;providers 库本身
// 仅在 Qt>=5 构建编译,Qt3 引用方走 stub,见 qxv_compat_guard.h)
#ifdef QT3_BUILD
#include <qglobal.h>        // Qt3:QtGlobal 还没改名
#else
#include <QtGlobal>
#endif

// ── 字符串字面量 ──────────────────────────────────────────────
// 项目口径:字符串全用 utf8。统一走 qlcomp 的 qFromUtf8("..."),
// 禁 QLatin1String/QLatin1Char/QStringLiteral/QXV_STR(已废弃)。

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
