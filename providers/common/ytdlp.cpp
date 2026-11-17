#include "ytdlp.h"

#include "common/debuglog.h"
#include "common/qcjson.h"

#include <qcoreapplication.h>
#include <qfileinfo.h>
#include <qprocess.h>
#include <qstandardpaths.h>

// 说明见 ytdlp.h。用 QProcess + finished 信号(不阻塞调用线程);
// 未安装/失败时 Info.ok=false,调用方回落 itag18。

namespace qxv {
namespace ytdlp {

static const char* kMod = "ytdlp";

bool enabled() {
    const QString v = qenv("QXVOTUBE_YT_YTDLP");
    if (v == qFromUtf8("0")) return false;
    if (v == qFromUtf8("1")) return true;
    // 自动探测
    static const bool probe = !QStandardPaths::findExecutable(
                                   qFromUtf8("yt-dlp")).isEmpty();
    return probe;
}

void fetch(const QString& url, const QString& proxy,
           std::function<void(const Info&)> cb) {
    Info fail;
    if (!enabled()) {
        fail.err = qFromUtf8("yt-dlp 未启用/未安装");
        cb(fail);
        return;
    }
    if (!QCoreApplication::instance()) {
        fail.err = qFromUtf8("yt-dlp 需要 QCoreApplication 事件循环");
        cb(fail);
        return;
    }

    auto* p = new QProcess();
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QStringList args = { qFromUtf8("-J"), qFromUtf8("--no-warnings"),
                         qFromUtf8("--no-playlist"),
                         qFromUtf8("--skip-download") };
    if (!proxy.isEmpty())
        args << qFromUtf8("--proxy") << proxy;
    args << url;

    QObject::connect(p, &QProcess::errorOccurred, p,
                     [p, cb](QProcess::ProcessError) {
        Info r;
        r.err = qFromUtf8("yt-dlp 启动失败: %1").arg(p->errorString());
        DBG_P(kMod, Warn, "%s", qToUtf8(r.err).data());
        p->deleteLater();
        cb(r);
    });
    QObject::connect(
        p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
        [p, cb](int code, QProcess::ExitStatus) {
        Info r;
        const QByteArray out = p->readAllStandardOutput();
        const QByteArray errOut = p->readAllStandardError();
        if (code != 0 || out.isEmpty()) {
            r.err = qFromUtf8("yt-dlp exit=%1 %2")
                        .arg(code)
                        .arg(QString::fromUtf8(errOut.left(300)));
            DBG_P(kMod, Warn, "%s", qToUtf8(r.err).data());
            p->deleteLater();
            cb(r);
            return;
        }
        JRoot root(out);
        if (!root.ok()) {
            r.err = qFromUtf8("yt-dlp JSON 解析失败");
            p->deleteLater();
            cb(r);
            return;
        }
        r.title = jstr(root.p, "title");
        cJSON* formats = jarr(root.p, "formats");
        for (int i = 0; i < jsize(formats); ++i) {
            const cJSON* f = jat(formats, i);
            Format fmt;
            fmt.id = jstr(f, "format_id");
            fmt.height = int(jint(f, "height"));
            fmt.ext = jstr(f, "ext");
            fmt.proto = jstr(f, "protocol");
            fmt.vcodec = jstr(f, "vcodec");
            fmt.acodec = jstr(f, "acodec");
            fmt.abr = double(jint(f, "abr"));
            fmt.tbr = double(jint(f, "tbr"));
            fmt.url = jstr(f, "url");
            if (r.hlsMaster.isEmpty())
                r.hlsMaster = jstr(f, "manifest_url");
            if (fmt.hasUrl()) r.formats.append(fmt);
        }
        r.ok = !r.formats.isEmpty() || !r.hlsMaster.isEmpty();
        if (!r.ok) r.err = qFromUtf8("yt-dlp 未返回可播 URL");
        DBG_P(kMod, Info, "yt-dlp formats=%d (title=%s)", r.formats.size(),
              qPrintable(r.title.left(20)));
        p->deleteLater();
        cb(r);
    });
    p->start(qFromUtf8("yt-dlp"), args);
}

} // namespace ytdlp
} // namespace qxv
