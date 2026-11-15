#include "debuglog.h"
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <cstdarg>
#include <cstdio>

// 跨 Qt 版本兼容(QXV_STR/SkipEmptyParts 等统一宏,详见 compat.h)
#include "compat.h"

namespace dbg {

static QHash<QString, Level> s_modLevel;   // 模块→级别;缺省模块 "" = 全局
static QFile s_logFile;
static bool s_fullMode = false;

static Level parseLevel(const QString& s) {
    // Qt3 无 compare(QString,Qt::CaseInsensitive):转大写比较
    const QString t = s.toUpper();
    if (t == QXV_STR("ERR"))   return Err;
    if (t == QXV_STR("WARN"))  return Warn;
    if (t == QXV_STR("INFO"))  return Info;
    if (t == QXV_STR("DEBUG")) return Debug;
    return Trace;
}

void loadFromEnv() {
    const QString d = QXV_ENV("QXVOTUBE_DEBUG");
    if (!d.isEmpty()) {
        const QStringList segs = d.split(QXV_STR(","), QXV_SKIP_EMPTY);
        for (const QString& seg : segs) {
            const int c = seg.indexOf(QXV_STR(":"));
            if (c < 0) { s_modLevel[QString()] = parseLevel(seg); continue; }
            s_modLevel[seg.left(c)] = parseLevel(seg.mid(c + 1));
        }
        DBG(Info, "log", "debug switches loaded from env: %s", qPrintable(d));
    }
    s_fullMode = !QXV_ENV_EMPTY("QXVOTUBE_DEBUG_FULL");
}

bool enabled(Level lvl, const char* module) {
    Level base = s_modLevel.value(QString(), Warn);
    Level m = s_modLevel.value(QString::fromLatin1(module), base);
    return lvl <= m;
}

void setLevel(const QString& module, Level lvl) { s_modLevel[module] = lvl; }

void setLogFile(const QString& path) {
    if (s_logFile.isOpen()) s_logFile.close();
    if (path.isEmpty()) return;
    s_logFile.setFileName(path);
    s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
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

    const QString line_ = QString("[PROVIDERS][%1][%2] %3")
        .arg(module, lvlName(lvl), QString::fromUtf8(buf));
    qWarning("%s [%s:%d]", qPrintable(line_), file, line);
    if (s_logFile.isOpen()) {
        QTextStream t(&s_logFile);
        t << QDateTime::currentDateTime().toString(Qt::ISODate) << ' '
          << line_ << '\n';
        t.flush();
    }
}

QString mask(const QString& v) {
    if (s_fullMode || v.size() <= 8) return v;
    return v.left(8) + QXV_STR("...");
}

} // namespace dbg
