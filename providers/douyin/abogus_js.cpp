// a_bogus 双机制对照 —— quickjs-ng 命令行执行路径(机制 B)
// 说明见 abogus_js.h。进程方式:QProcess 起 "qjs -C runner.js <seed> <now>
// <uri> <ua> <windowFp>"(不链接 quickjs 库;系统没有 qjs 命令时自动跳过
// 对照,仅警告不阻塞签名)。Qt3/Qt4 QProcess API 差异分支同 ytdlp.cpp 口径。
#include "abogus_js.h"
#include "qjsrun.h"
#include "abogus.h"
#include "compat.h"
#include <qapplication.h>
#include <qdir.h>
#include <qdatetime.h>
#include <qfile.h>
#include <qprocess.h>
#include <stdlib.h>
#include <string.h>

namespace qxv { namespace abogus {

namespace {

// LCG 与 node/qjs 金标 harness 相同(seed*1103515245+12345)/2^32
struct Lcg {
    unsigned int s;
    explicit Lcg(unsigned int seed) : s(seed) {}
    double next() {
        s = (unsigned int)((unsigned long long)s * 1103515245ull + 12345ull);
        return s / 4294967296.0;
    }
};

double lcgThunk(void* ctx) { return ((Lcg*)ctx)->next(); }

// 目录回退:env QXVOTUBE_ABJS_DIR → appDir/abjs → appDir/../../providers/douyin/abjs
QString findRunner()
{
    const char* env = getenv("QXVOTUBE_ABJS_DIR");
    if (env != 0) {
        QDir d(QString::fromLocal8Bit(env));
        if (QFile::exists(d.filePath(qFromUtf8("runner.js"))))
            return d.filePath(qFromUtf8("runner.js"));
    }
    const QDir app = qApp->applicationDirPath();   // Qt3 为实例方法
    if (QFile::exists(app.filePath(qFromUtf8("abjs/runner.js"))))
        return app.filePath(qFromUtf8("abjs/runner.js"));
    QDir src = app;
    if (src.cd(qFromUtf8("../../providers/douyin/abjs")) &&
        QFile::exists(src.filePath(qFromUtf8("runner.js"))))
        return src.filePath(qFromUtf8("runner.js"));
    return QString();
}

// 从 stdout 解析 "AB=<a_bogus>" 行(取最后一行,防前导噪音)
std::string parseAb(const std::string& s)
{
    size_t pos = s.rfind("AB=");
    if (pos == std::string::npos) return std::string();
    size_t e = pos + 3;
    while (e < s.size() && s[e] != '\n' && s[e] != '\r') ++e;
    return s.substr(pos + 3, e - (pos + 3));
}

} // namespace

bool generateViaJs(unsigned int seed, long long nowMs,
                   const std::string& uri, const std::string& ua,
                   const char* windowFp, std::string* out)
{
    static bool warnedOnce = false;
    const QString runner = findRunner();
    if (runner.isEmpty()) {
        if (!warnedOnce) {
            qWarning("[douyin] a_bogus JS 对照跳过:runner.js 未找到"
                     "(仅对照缺席,不影响 C++ 签名)");
            warnedOnce = true;
        }
        return false;
    }
    const char* exeEnv = getenv("QXVOTUBE_QJS");
    const QString exe = QString::fromLocal8Bit(exeEnv != 0 ? exeEnv : "qjs");
    const QString fpStr = windowFp != 0
        ? QString::fromLocal8Bit(windowFp)
        : QString::fromLocal8Bit("1920|1080|1920|1040|1920|1040|0|0|Win32");
    // argv 传参(uri/ua 无引号问题,QProcess 不经 shell;uri 有 URL 转义无空格,
    // ua 含空格但作为单个 argv 项传递)
    const QStringList args = QStringList()
        << exe
        << qFromUtf8("-C") << runner
        << QString::number((unsigned long)seed)
        << QString::number((long long)nowMs)
        << QString::fromUtf8(uri.c_str())
        << QString::fromUtf8(ua.c_str())
        << fpStr;

    // qjs 执行器统一走 qjsrun(详见 qjsrun.h:Qt3 fork/execvp,
    // Qt4+ QProcess;均剔除 LD_LIBRARY_PATH,防 vcpkg 库遮蔽)
    std::string outBuf;
    const bool ok = qxv::douyin::runQjs(args, &outBuf, 2000);

    if (!ok) {
        if (!warnedOnce) {
            // 最常见原因:系统未装 quickjs-ng(qjs 命令不存在)
            qWarning("[douyin] a_bogus JS 对照跳过:qjs 不存在/超时/异常"
                     "(仅对照缺席,不影响 C++ 签名;装 quickjs-ng 后自动恢复)");
            warnedOnce = true;
        }
        return false;
    }
    std::string ab = parseAb(outBuf);
    if (ab.empty()) {
        if (!warnedOnce) {
            qWarning("[douyin] a_bogus JS 对照跳过:qjs 无 AB= 输出(仅对照缺席)");
            warnedOnce = true;
        }
        return false;
    }
    if (out != 0) *out = ab;
    return true;
}

std::string generateChecked(const std::string& uri, const std::string& ua,
                            long long nowMs, unsigned int seed,
                            const char* windowFp)
{
    // 机制 A(权威):同 seed 走纯 C++ 移植
    Lcg lcg(seed);
    std::string cpp = generate(uri, ua, nowMs, &lcg, lcgThunk, windowFp);

    // 对照开关:QXVOTUBE_ABJS_CHECK=0 关闭(默认开)
    const char* chk = getenv("QXVOTUBE_ABJS_CHECK");
    if (chk != 0 && qstricmp(chk, "0") == 0)
        return cpp;

    // 机制 B(对照):同 seed/now/uri/ua/fp 跑参考原版 JS,字节级应一致
    std::string js;
    if (generateViaJs(seed, nowMs, uri, ua, windowFp, &js) && js != cpp) {
        // 不匹配:以 C++ 结果为准,qWarning 取证(两侧值+入参指纹)
        qWarning("[douyin] a_bogus 双机制不匹配(以 C++ 结果为准)");
        qWarning("[douyin]   cpp=%s", cpp.c_str());
        qWarning("[douyin]   js =%s", js.c_str());
        qWarning("[douyin]   now=%lld seed=%u uriLen=%d uaLen=%d fp=%s",
                 nowMs, seed, (int)uri.size(), (int)ua.size(),
                 windowFp != 0 ? windowFp : "default");
    }
    return cpp;
}

}} // namespace qxv::abogus
