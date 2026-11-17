#include "debuglog.h"
#include <map>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// 跨 Qt 版本兼容(QXV_STR/SkipEmptyParts 等统一宏,详见 compat.h)
#include "compat.h"

namespace dbg {

static std::map<std::string, Level> s_modLevel;   // 模块→级别;缺省 "" = 全局
static FILE* s_logFile = 0;
static bool s_fullMode = false;

static std::string keyOf(const QString& s) {
    return std::string(qToUtf8(s).data());
}

static Level parseLevel(const QString& s) {
    const QString t = qToUpper(s);
    if (t == qFromUtf8("ERR"))   return Err;
    if (t == qFromUtf8("WARN"))  return Warn;
    if (t == qFromUtf8("INFO"))  return Info;
    if (t == qFromUtf8("DEBUG")) return Debug;
    return Trace;
}

void loadFromEnv() {
    const QString d = qenv("QXVOTUBE_DEBUG");
    if (!d.isEmpty()) {
        const QStringList segs = qStrSplit(d, qFromUtf8(","), true);
        for (int i = 0; i < segs.size(); ++i) {
            const QString& seg = segs[i];
            const int c = qIndexOf(seg, qFromUtf8(":"));
            if (c < 0) { s_modLevel[std::string()] = parseLevel(seg); continue; }
            s_modLevel[keyOf(seg.left(c))] = parseLevel(seg.mid(c + 1));
        }
        DBG(Info, "log", "debug switches loaded from env: %s",
            qToUtf8(d).data());
    }
    s_fullMode = !qenv_empty("QXVOTUBE_DEBUG_FULL");
}

bool enabled(Level lvl, const char* module) {
    Level base = Warn;
    std::map<std::string, Level>::const_iterator d = s_modLevel.find(std::string());
    if (d != s_modLevel.end()) base = d->second;
    Level m = base;
    std::map<std::string, Level>::const_iterator it =
        s_modLevel.find(std::string(module));
    if (it != s_modLevel.end()) m = it->second;
    return lvl <= m;
}

void setLevel(const QString& module, Level lvl) {
    s_modLevel[keyOf(module)] = lvl;
}

void setLogFile(const QString& path) {
    if (s_logFile) { std::fclose(s_logFile); s_logFile = 0; }
    if (path.isEmpty()) return;
    s_logFile = std::fopen(qToUtf8(path).data(), "a");
}

static const char* lvlName(Level l) {
    static const char* k[] = { "Err", "Warn", "Info", "Debug", "Trace" };
    return k[l];
}

void vlog(Level lvl, const char* module, const char* file, int line,
          const char* fmt, ...) {
    if (!enabled(lvl, module)) return;
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    const QString line_ = QString::fromUtf8("[PROVIDERS][%1][%2] %3")
        .arg(qFromUtf8(module), qFromUtf8(lvlName(lvl)), qFromUtf8(buf));
    qWarning("%s [%s:%d]", qToUtf8(line_).data(), file, line);
    if (s_logFile) {
        std::fprintf(s_logFile, "%s\n", qToUtf8(line_).data());
        std::fflush(s_logFile);
    }
}

QString mask(const QString& v) {
    if (s_fullMode || v.length() <= 8) return v;
    return v.left(8) + qFromUtf8("...");
}

} // namespace dbg
