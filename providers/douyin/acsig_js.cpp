// __ac_signature 双机制对照 —— quickjs-ng 命令行执行路径(机制 B)
// 说明见 acsig_js.h。QProcess 起
//   "qjs -C acsig_runner.js <ts> <site> <nonce> <ua>"
// 不链接 quickjs 库;qjs 缺失自动跳过对照(仅警告)。Qt3/Qt4 分支同
// abogus_js.cpp 口径。
#include "acsig_js.h"
#include "qjsrun.h"
#include "acsig.h"
#include "common/compat.h"
#include <qapplication.h>
#include <qdir.h>
#include <qdatetime.h>
#include <qevent.h>
#include <qfile.h>
#include <stdlib.h>
#include <string.h>


namespace qxv { namespace douyin {

namespace {

// 目录回退:env QXVOTUBE_ABJS_DIR → appDir/abjs → appDir/../../providers/douyin/abjs
QString findRunner()
{
    const char* env = getenv("QXVOTUBE_ABJS_DIR");
    if (env != 0) {
        QDir d(QString::fromLocal8Bit(env));
        if (QFile::exists(d.filePath(qFromUtf8("acsig_runner.js"))))
            return d.filePath(qFromUtf8("acsig_runner.js"));
    }
    const QDir app = qApp->applicationDirPath();   // Qt3 为实例方法
    if (QFile::exists(app.filePath(qFromUtf8("abjs/acsig_runner.js"))))
        return app.filePath(qFromUtf8("abjs/acsig_runner.js"));
    QDir src = app;
    if (src.cd(qFromUtf8("../../providers/douyin/abjs")) &&
        QFile::exists(src.filePath(qFromUtf8("acsig_runner.js"))))
        return src.filePath(qFromUtf8("acsig_runner.js"));
    return QString();
}

// 从 stdout 解析 "SIG=<签名>" 行(取最后一条,防前导噪音)
QString parseSig(const std::string& s)
{
    const size_t pos = s.rfind("SIG=");
    if (pos == std::string::npos) return QString();
    size_t e = pos + 4;
    while (e < s.size() && s[e] != '\n' && s[e] != '\r') ++e;
    return QString::fromUtf8(s.c_str() + pos + 4, (int)(e - (pos + 4)));
}

// 异步对照任务:调用点常在 NetClient 回调内,Qt3 下那里再 processEvents
// 属嵌套事件循环(会拿不到 qjs 输出)→ postEvent 回到正常事件循环再跑,
// 仅做诊断日志,不阻塞签名链路(签名已由 C++ 同步返回)。
const QEvent::Type kAcsigCompareEvt = (QEvent::Type)(QEvent::User + 919);

class AcCompareJob : public QObject
{
public:
    QString cpp, site, nonce, ua;
    long long ts;
    void kick() { QApplication::postEvent(this, new QEvent(kAcsigCompareEvt)); }
protected:
    bool event(QEvent* e) {
        if (e->type() != kAcsigCompareEvt) return QObject::event(e);
        // 先拷贝:对照期间可能又有新任务覆盖字段
        const QString c = cpp, st = site, no = nonce, u = ua;
        const long long t = ts;
        QString js;
        if (acSignatureViaJs(t, st, no, u, &js) && js != c) {
            // 不匹配:以 C++ 结果为准,qWarning 取证(两侧值+入参)
            qWarning("[douyin] acsig 双机制不匹配(以 C++ 结果为准)");
            qWarning("[douyin]   cpp=%s", qToUtf8(c).data());
            qWarning("[douyin]   js =%s", qToUtf8(js).data());
            qWarning("[douyin]   ts=%lld site=%s nonceLen=%d uaLen=%d",
                     t, qToUtf8(st).data(), (int)no.length(), (int)u.length());
        }
        return true;
    }
};

// 进程内常驻(不释放);同一时刻只保留最后一组入参
void scheduleCompare(const QString& cpp, long long ts, const QString& site,
                     const QString& nonce, const QString& ua)
{
    static AcCompareJob* job = 0;
    if (job == 0) job = new AcCompareJob();
    job->cpp = cpp; job->ts = ts; job->site = site;
    job->nonce = nonce; job->ua = ua;
    job->kick();
}

} // namespace

bool acSignatureViaJs(long long timeSec, const QString& site,
                      const QString& nonce, const QString& ua, QString* out)
{
    static bool warnedOnce = false;
    const QString runner = findRunner();
    if (runner.isEmpty()) {
        if (!warnedOnce) {
            qWarning("[douyin] acsig JS 对照跳过:acsig_runner.js 未找到"
                     "(仅对照缺席,不影响 C++ 签名)");
            warnedOnce = true;
        }
        return false;
    }
    const char* exeEnv = getenv("QXVOTUBE_QJS");
    const QString exe = QString::fromLocal8Bit(exeEnv != 0 ? exeEnv : "qjs");
    const QStringList args = QStringList()
        << exe
        << qFromUtf8("-C") << runner
        << QString::number((long long)timeSec)
        << site << nonce << ua;

    std::string outBuf;
    bool ok = false;
    // qjs 执行器统一走 qjsrun(详见 qjsrun.h:Qt3 fork/execvp,
    // Qt4+ QProcess;均剔除 LD_LIBRARY_PATH,防 vcpkg 库遮蔽)
    ok = runQjs(args, &outBuf, 2000);

    if (!ok) {
        if (!warnedOnce) {
            qWarning("[douyin] acsig JS 对照跳过:qjs 不存在/超时/异常"
                     "(仅对照缺席,不影响 C++ 签名;装 quickjs-ng 后自动恢复)");
            warnedOnce = true;
        }
        return false;
    }
    const QString sig = parseSig(outBuf);
    if (sig.isEmpty()) {
        if (!warnedOnce) {
            qWarning("[douyin] acsig JS 对照跳过:qjs 无 SIG= 输出(仅对照缺席)");
            warnedOnce = true;
        }
        return false;
    }
    if (out != 0) *out = sig;
    return true;
}

QString acSignatureChecked(long long timeSec, const QString& site,
                           const QString& nonce, const QString& ua)
{
    // 机制 A(权威):纯 C++ 纯算(同步返回,签名链路不等 qjs)
    const QString cpp = acSignature(timeSec, site, nonce, ua);

    // 对照开关复用既有变量:QXVOTUBE_ABJS_CHECK=0 关闭(默认开)
    const char* chk = getenv("QXVOTUBE_ABJS_CHECK");
    if (chk != 0 && qstricmp(chk, "0") == 0)
        return cpp;

    // 机制 B(对照):异步执行(关键)。调用点常在 NetClient 回调里,Qt3 下
    // 该上下文再 processEvents 属嵌套,会拿不到 qjs 输出 → 用 postEvent
    // 回到正常事件循环的一轮再跑,只做诊断日志,不阻塞签名。
    scheduleCompare(cpp, timeSec, site, nonce, ua);
    return cpp;
}

}} // namespace qxv::douyin
