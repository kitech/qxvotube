#include "ytdlp.h"

#include "common/debuglog.h"
#include "common/qcjson.h"

#ifdef QT3_BUILD
#include <qapplication.h>      // Qt3:QCoreApplication 不存在
#else
#include <qcoreapplication.h>
#endif
#include <qfileinfo.h>
#include <qprocess.h>

// 说明见 ytdlp.h。用 QProcess(不阻塞调用线程);未安装/失败时 Info.ok=false,
// 调用方回落 progressive(itag18)。
//
// Qt3/Qt4+ 差异:
//   Qt3   start() 返回 bool;信号 readyReadStdout/processExited;
//         读取 readStdout/readStderr;无 exitCode(normalExit 判定)
//   Qt4+  start(exe,args) 无返回;信号 readyReadStandardOutput/finished;
//         读取 readAllStandardOutput/readAllStandardError

namespace qxv {
namespace ytdlp {

static const char* kMod = "ytdlp";

// PATH 扫描(Qt3 无 QStandardPaths)
static QString findInPath(const QString& exe) {
    const QStringList dirs = qStrSplit(qenv("PATH"), qFromUtf8(":"), true);
    for (int i = 0; i < dirs.size(); ++i) {
        const QString f = dirs[i] + qFromUtf8("/") + exe;
        if (QFileInfo(f).isExecutable()) return f;
    }
    return QString();
}

bool enabled() {
    const QString v = qenv("QXVOTUBE_YT_YTDLP");
    if (v == qFromUtf8("0")) return false;
    if (v == qFromUtf8("1")) return true;
    static const bool probe = !findInPath(qFromUtf8("yt-dlp")).isEmpty();
    return probe;
}

static void parseJson(const QByteArray& out, Info* r) {
    JRoot root(out);
    if (!root.ok()) { r->err = qFromUtf8("yt-dlp JSON 解析失败"); return; }
    r->title = jstr(root.p, "title");
    // 字幕轨道:subtitles=人工, automatic_captions=自动;优先 json3,其次 vtt/srv3
    const char* maps[2] = { "subtitles", "automatic_captions" };
    for (int m = 0; m < 2; ++m) {
        cJSON* obj = jobj(root.p, maps[m]);
        for (cJSON* it = obj ? obj->child : 0; it; it = it->next) {
            if (!cJSON_IsArray(it)) continue;
            QString url, ext, name;
            for (int i = 0; i < jsize(it); ++i) {
                const cJSON* f = jat(it, i);
                const QString e = jstr(f, "ext");
                if (e == qFromUtf8("json3")) { url = jstr(f, "url"); ext = e;
                    name = jstr(f, "name"); break; }
                if (url.isEmpty() && (e == qFromUtf8("vtt") || e == qFromUtf8("srv3"))) {
                    url = jstr(f, "url"); ext = e; name = jstr(f, "name");
                }
            }
            if (url.isEmpty()) continue;
            SubTrackInfo t;
            t.lang = qFromUtf8(it->string ? it->string : "");
            t.name = name; t.ext = ext; t.url = url;
            t.automatic = (m == 1);
            r->subTracks.append(t);
        }
    }
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
        if (r->hlsMaster.isEmpty()) r->hlsMaster = jstr(f, "manifest_url");
        if (fmt.hasUrl()) r->formats.append(fmt);
    }
    r->ok = !r->formats.isEmpty() || !r->hlsMaster.isEmpty();
    if (!r->ok) r->err = qFromUtf8("yt-dlp 未返回可播 URL");
}

// YtdlpProc 实现(Qt3/Qt4+ 命名差异见头文件注释)
YtdlpProc::YtdlpProc(const QString& exe, const QStringList& args,
                     std::function<void(const Info&)> cb)
    : m_p(0), m_cb(cb), m_done(false)
{
    m_p = new QProcess();
#ifndef QT3_BUILD
    m_p->setProcessChannelMode(QProcess::SeparateChannels);
#endif
#ifdef QT3_BUILD
    QStringList all;
    all << exe;
    for (int i = 0; i < args.size(); ++i) all << args[i];
    m_p->setArguments(all);
    QObject::connect(m_p, SIGNAL(readyReadStdout()), this, SLOT(drain()));
    QObject::connect(m_p, SIGNAL(readyReadStderr()), this, SLOT(drain()));
    QObject::connect(m_p, SIGNAL(processExited()), this, SLOT(exited()));
#else
    m_args << exe << args;
    QObject::connect(m_p, SIGNAL(readyReadStandardOutput()),
                     this, SLOT(drain()));
    QObject::connect(m_p, SIGNAL(readyReadStandardError()),
                     this, SLOT(drain()));
    QObject::connect(m_p, SIGNAL(finished(int, QProcess::ExitStatus)),
                     this, SLOT(exited()));
#endif
}

YtdlpProc::~YtdlpProc() { delete m_p; }

bool YtdlpProc::start()
{
#ifdef QT3_BUILD
    return m_p->start();
#else
    m_p->start(m_args.first(), m_args.mid(1));
    return m_p->state() != QProcess::NotRunning;
#endif
}

void YtdlpProc::drain()
{
    QByteArray chunk;
#ifdef QT3_BUILD
    chunk = m_p->readStdout();
    if (!chunk.isEmpty()) m_out.append(chunk.data(), (size_t)chunk.size());
    chunk = m_p->readStderr();
    if (!chunk.isEmpty()) m_err.append(chunk.data(), (size_t)chunk.size());
#else
    chunk = m_p->readAllStandardOutput();
    if (!chunk.isEmpty()) m_out.append(chunk.constData(), (size_t)chunk.size());
    chunk = m_p->readAllStandardError();
    if (!chunk.isEmpty()) m_err.append(chunk.constData(), (size_t)chunk.size());
#endif
}

void YtdlpProc::exited()
{
    if (m_done) return;
    m_done = true;
    drain();
    Info r;
#ifdef QT3_BUILD
    const int code = m_p->normalExit() ? 0 : -1;
#else
    const int code = m_p->exitStatus() == QProcess::NormalExit
                         ? m_p->exitCode() : -1;
#endif
    if (code != 0 || m_out.empty()) {
        const int n = (int)m_err.size() > 300 ? 300 : (int)m_err.size();
        r.err = qFromUtf8("yt-dlp exit=%1 %2")
                    .arg(code).arg(qFromUtf8(m_err.data(), n));
        DBG_P(kMod, Warn, "%s", qToUtf8(r.err).data());
    } else {
        parseJson(qbaFromData(m_out.data(), (int)m_out.size()), &r);
        if (r.ok)
            DBG_P(kMod, Info, "yt-dlp ok formats=%d", r.formats.size());
        else
            DBG_P(kMod, Warn, "yt-dlp failed: %s", qToUtf8(r.err).data());
    }
    std::function<void(const Info&)> cb = m_cb;
    m_cb = std::function<void(const Info&)>();
    if (cb) cb(r);
    deleteLater();
}

void fetch(const QString& url, const QString& proxy,
           std::function<void(const Info&)> cb) {
    Info fail;
    if (!enabled()) {
        fail.err = qFromUtf8("yt-dlp 未启用/未安装");
        cb(fail);
        return;
    }
    #ifdef QT3_BUILD
    if (!qApp) {
#else
    if (!QCoreApplication::instance()) {
#endif
        fail.err = qFromUtf8("yt-dlp 需要 QCoreApplication 事件循环");
        cb(fail);
        return;
    }
    QStringList args;
    args << qFromUtf8("-J") << qFromUtf8("--no-warnings")
         << qFromUtf8("--no-playlist") << qFromUtf8("--skip-download");
    if (!proxy.isEmpty())
        args << qFromUtf8("--proxy") << proxy;
    args << url;

    YtdlpProc* p = new YtdlpProc(findInPath(qFromUtf8("yt-dlp")), args, cb);
    if (!p->start()) {
        Info r;
        r.err = qFromUtf8("yt-dlp 启动失败");
        DBG_P(kMod, Warn, "%s", qToUtf8(r.err).data());
        delete p;
        cb(r);
    }
}

} // namespace ytdlp
} // namespace qxv
