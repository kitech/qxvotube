#include "downloader.h"

#include <qapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfile.h>
#include <qprocess.h>
#include <qregexp.h>
#ifdef QT3_BUILD
#include <qstringlist.h>
#endif
#include <stdio.h>

namespace {

QString sanitizeName(const QString& s)
{
    QString t;
    for (int i = 0; i < (int)s.length() && t.length() < 80; ++i) {
        const QChar c = s[i];
        if (c == QChar('/') || c == QChar('\\') || c == QChar(':')
            || c == QChar('*') || c == QChar('?') || c == QChar('"')
            || c == QChar('<') || c == QChar('>') || c == QChar('|'))
            continue;
        t += c;
    }
    t = qTrim(t);
    return t.isEmpty() ? qFromUtf8("video") : t;
}

QString defaultDir()
{
#ifdef QT3_BUILD
    const QString home = QDir::homeDirPath();
#else
    const QString home = QDir::homePath();
#endif
    return home + qFromUtf8("/Downloads/qlvotube");
}

} // namespace

Downloader::Downloader(QObject* parent)
    : QObject(parent), m_proc(0), m_durMs(0), m_lastPct(-1)
{
}

Downloader::~Downloader() { cancel(); }

bool Downloader::start(const QString& url, const QString& title, long durationMs,
                       ProgressFn onPct, DoneFn onDone, const QString& outDir,
                       const QString& referer, const QString& ua,
                       const QString& proxy)
{
    if (m_proc != 0) return false;   // 并发 1
    if (url.isEmpty()) return false;
    QString dir = outDir.isEmpty() ? defaultDir() : outDir;
    QDir d(dir);
    if (!d.exists()) d.mkdir(dir);
    QString out = dir + QChar('/') + sanitizeName(title) + qFromUtf8(".mp4");
    /* 同名避开覆盖 */
    if (QFile::exists(out)) {
        for (int i = 2; i < 1000; ++i) {
            QString cand = dir + QChar('/') + sanitizeName(title)
                         + qFromUtf8(" (%1).mp4").arg(i);
            if (!QFile::exists(cand)) { out = cand; break; }
        }
    }

    m_out = out;
    m_durMs = durationMs;
    m_lastPct = -1;
    m_errTail = QString();
    m_onPct = onPct;
    m_onDone = onDone;

    QStringList args;
    args << qFromUtf8("ffmpeg")
         << qFromUtf8("-nostdin") << qFromUtf8("-y");
    if (!ua.isEmpty())
        args << qFromUtf8("-user_agent") << ua;
    if (!referer.isEmpty())
        args << qFromUtf8("-headers")
             << qFromUtf8("Referer: %1\r\n").arg(referer);
    if (!proxy.isEmpty())
        args << qFromUtf8("-http_proxy") << proxy;
    args << qFromUtf8("-i") << url
         << qFromUtf8("-c") << qFromUtf8("copy")
         << qFromUtf8("-progress") << qFromUtf8("pipe:1")
         << qFromUtf8("-nostats")
         << out;
#ifdef QT3_BUILD
    QProcess* p = new QProcess(this);
    p->setArguments(args);              /* Qt3: args[0]=程序 */
    QObject::connect(p, SIGNAL(readyReadStdout()), this, SLOT(onProgressData()));
    QObject::connect(p, SIGNAL(readyReadStderr()), this, SLOT(onProgressData()));
    QObject::connect(p, SIGNAL(processExited()), this, SLOT(onExited()));
    if (!p->start()) { delete p; return false; }
#else
    QProcess* p = new QProcess(this);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(p, SIGNAL(readyReadStandardOutput()), this, SLOT(onProgressData()));
    QObject::connect(p, SIGNAL(readyReadStandardError()), this, SLOT(onProgressData()));
    QObject::connect(p, SIGNAL(finished(int, QProcess::ExitStatus)),
                     this, SLOT(onExited()));
    p->start(args.first(), args.mid(1));
    if (!p->waitForStarted(3000)) { delete p; return false; }
#endif
    m_proc = p;
    emitPct(0);
    return true;
}

void Downloader::cancel()
{
    if (m_proc == 0) return;
    QProcess* p = m_proc;
    m_proc = 0;
#ifdef QT3_BUILD
    if (p->isRunning()) p->kill();
    p->disconnect(this);
    p->deleteLater();
#else
    if (p->state() != QProcess::NotRunning) p->kill();
    p->disconnect(this);
    p->deleteLater();
#endif
}

void Downloader::emitPct(int pct)
{
    if (pct == m_lastPct) return;
    m_lastPct = pct;
    if (m_onPct) m_onPct(pct);
}

void Downloader::onProgressData()
{
    if (m_proc == 0) return;
    QByteArray buf;
#ifdef QT3_BUILD
    buf = m_proc->readStdout();
    const QByteArray err = m_proc->readStderr();
#else
    buf = m_proc->readAllStandardOutput();
    const QByteArray err = m_proc->readAllStandardError();
#endif
    if (!err.isEmpty()) {
        const QString e = QString::fromUtf8(err);
        m_errTail += e;
        if (m_errTail.length() > 2000) m_errTail = m_errTail.right(2000);
    }
    if (buf.isEmpty()) return;
    const QString txt = QString::fromUtf8(buf);
    int at = 0;
    while ((at = qIndexOf(txt, qFromUtf8("out_time_ms="), at)) >= 0) {
        at += 12;
        const int eol = qIndexOf(txt, qFromUtf8("\n"), at);
        const QString num = (eol > 0) ? qSimplified(txt.mid(at, eol - at))
                                      : qSimplified(txt.mid(at));
        bool ok = false;
        const long long us = num.toLongLong(&ok);
        if (!ok) continue;
        if (m_durMs > 0) {
            int pct = (int)(us / 1000 * 100 / m_durMs);
            if (pct < 0) pct = 0;
            if (pct > 99) pct = 99;      /* 100 留给进程正常退出 */
            emitPct(pct);
        } else {
            emitPct(-1);                 /* 未知时长:未定态 */
        }
    }
}

void Downloader::onExited()
{
    if (m_proc == 0) return;
    QProcess* p = m_proc;
    m_proc = 0;
    bool ok = false;
#ifdef QT3_BUILD
    ok = p->normalExit() && p->exitStatus() == 0;
#else
    ok = p->exitStatus() == QProcess::NormalExit && p->exitCode() == 0;
#endif
    p->disconnect(this);
    p->deleteLater();
    if (ok) {
        emitPct(100);
        finish(true, QString());
    } else {
        emitPct(-2);
        QString err = qTrim(m_errTail);
        if (err.length() > 300) err = err.right(300);
        finish(false, err.isEmpty() ? qFromUtf8("ffmpeg exit != 0") : err);
    }
}

void Downloader::finish(bool ok, const QString& err)
{
    if (m_onDone) {
        DoneFn cb = m_onDone;
        m_onDone = DoneFn();
        m_onPct = ProgressFn();
        cb(ok, m_out, err);
    }
}
