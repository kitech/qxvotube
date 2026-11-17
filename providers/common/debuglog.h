#pragma once
#include <qstring.h>

// providers 统一调试日志:分级 + 分模块开关 + 敏感信息脱敏
// 开关来源(优先级):环境变量 QXVOTUBE_DEBUG > QSettings providers/debug* > 缺省 Err+Warn
// 格式:QXVOTUBE_DEBUG="http,sign,bilibili:debug"  (无冒号=全局级别,冒号=模块级)
//      QXVOTUBE_DEBUG_FULL=1  → mask() 返回全量(QA 取证用)
namespace dbg {

enum Level { Err = 0, Warn, Info, Debug, Trace };

void vlog(Level lvl, const char* module, const char* file, int line, const char* fmt, ...)
    __attribute__((format(printf, 5, 6)));
bool enabled(Level lvl, const char* module);   // 昂贵 dump 先判再调
void setLevel(const QString& module, Level lvl);
void setLogFile(const QString& path);          // 可选落盘,缺省关
void loadFromEnv();                            // 读 QXVOTUBE_DEBUG / QXVOTUBE_DEBUG_FULL

// 敏感值:前 8 字符 + "…"(cookie/token/seed/web_session 用)
QString mask(const QString& v);

} // namespace dbg

#define DBG(lv, mod, ...) dbg::vlog(dbg::lv, mod, __FILE__, __LINE__, __VA_ARGS__)
#define DBG_HTTP(lv, ...) DBG(lv, "http", __VA_ARGS__)
#define DBG_SIGN(lv, ...) DBG(lv, "sign", __VA_ARGS__)
#define DBG_DEV(lv, ...)  DBG(lv, "dev",  __VA_ARGS__)
#define DBG_AUTH(lv, ...) DBG(lv, "auth", __VA_ARGS__)
#define DBG_P(mod, lv, ...) DBG(lv, mod, __VA_ARGS__)   // 平台模块:如 "bilibili"

// Release 默认不编入 Trace 级(APK 提速)
#ifdef QXV_RELEASE_TRIM
#define DBG_TRACE(mod, ...) ((void)0)
#else
#define DBG_TRACE(mod, ...) DBG_P(mod, Trace, __VA_ARGS__)
#endif
