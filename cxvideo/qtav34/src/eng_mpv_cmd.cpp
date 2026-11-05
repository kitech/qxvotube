#include "eng_mpv_cmd.h"
#include "cxvideo_global.h"
#include <qprocess.h>
#include <qstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── 可用性探测:PATH 上找 mpv 可执行(unix;win32 留 M4)──

#ifndef _WIN32

#include <sys/wait.h>

static bool cxProbeMpvBinaryOnce(QString& outPath)
{
    FILE* fp = popen("command -v mpv 2>/dev/null", "r");
    if (fp == 0)
        return false;
    char buf[512];
    bool found = fgets(buf, sizeof(buf), fp) != 0;
    int rc = pclose(fp);
    (void)rc;
    if (!found)
        return false;
    buf[sizeof(buf) - 1] = 0;
    char* nl = strchr(buf, '\n');
    if (nl != 0)
        *nl = 0;
    if (buf[0] == '\0')
        return false;
    outPath = QString::fromLatin1(buf);
    return true;
}

#else

static bool cxProbeMpvBinaryOnce(QString& outPath)
{
    (void)outPath; // win32 M4 打包阶段再补 PATH 搜索
    return false;  // M1 不宣称可用
}

#endif

bool CXEngineMpvCmd::isAvailable() const
{
    static bool probed = false;
    static QString cached;
    if (!probed) {
        probed = true;
        cxProbeMpvBinaryOnce(cached);
    }
    return !cached.isEmpty();
}

// ── 生命周期 ──

CXEngineMpvCmd::CXEngineMpvCmd()
    : CXEngine(), p_proc(0), m_running(false), m_binPath("mpv")
{
    QString path;
    if (cxProbeMpvBinaryOnce(path))
        m_binPath = path;
}

CXEngineMpvCmd::~CXEngineMpvCmd()
{
    killProcess();
    m_url = QString();
}

static void cxMpvCmdAppendArgs(QStringList& args)
{
    // 进程型,输出静默,不抢键盘;播放结束即退出(keep-open=no)
    args += "--no-osc";
    args += "--no-terminal";
    args += "--really-quiet";
    args += "--input-default-bindings=no";
    args += "--input-vo-keyboard=no";
    args += "--pause=no";
    args += "--cache=yes";
    args += "--keep-open=no";
}

bool CXEngineMpvCmd::open(const QString& url)
{
    if (!isAvailable()) {
        emit noticedError((int)CxErrEngine, QString("mpv binary not found"));
        return false;
    }
    if (m_running)
        killProcess(); // 换源即重启进程

    m_url = url;
    m_running = false;

    QProcess* proc = 0;
    proc = new QProcess(this);

    QStringList args;
    cxMpvCmdAppendArgs(args);

    if (p_host != 0) {
        // wid 原生嵌入(与 libmpv 内核同等渲染路)
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)p_host->winId());
        args += QString("--wid=") + buf;
    }
    args += url;

#ifndef QT3_BUILD
    connect(proc, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(procFinished()));
    proc->start(m_binPath, args);
    bool didStart = (proc->state() != QProcess::NotRunning);
    if (!didStart) {
        // start 未成功(如 binary 缺位);回收
        delete proc;
        emit noticedError((int)CxErrOpenFail, m_url);
        return false;
    }
#else // QT3_BUILD
    // Qt3: QProcess::addArgument 逐个加入,start() 返回 bool
    const QStringList::Iterator itEnd = args.end();
    for (QStringList::Iterator it = args.begin(); it != itEnd; ++it)
        proc->addArgument(*it);
    connect(proc, SIGNAL(processExited()), this, SLOT(procFinished()));
    if (!proc->start()) {
        delete proc;
        emit noticedError((int)CxErrOpenFail, m_url);
        return false;
    }
#endif

    p_proc = proc;
    m_running = true;
    emit started(); // 进程已启动(M1 当时无 mpv 内部 loaded 事件;M2 接 IPC 后精化)
    return true;
}

void CXEngineMpvCmd::procFinished()
{
    m_running = false;
    if (p_proc != 0) {
#ifndef QT3_BUILD
        p_proc->deleteLater();
#else
        delete p_proc; // Qt3: 就地回收 Qt3 QProcess(已退完)
#endif
        p_proc = 0;
    }
    emit finished(); // 播完/退出(进程型最简映射)
}

void CXEngineMpvCmd::killProcess()
{
    if (p_proc == 0) {
        m_running = false;
        return;
    }
    QProcess* proc = p_proc;
    p_proc = 0;
    m_running = false;
#ifndef QT3_BUILD
    disconnect(proc, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(procFinished()));
    if (proc->state() != QProcess::NotRunning)
        proc->kill();
    proc->waitForFinished(1500);
    delete proc;
#else // QT3_BUILD
    disconnect(proc, SIGNAL(processExited()), this, SLOT(procFinished()));
    proc->tryTerminate();
    delete proc; // Qt3 QProcess 析构会回收残余进程
#endif
}

void CXEngineMpvCmd::close()
{
    killProcess();
    m_url = QString();
    emit stopped();
}

bool CXEngineMpvCmd::play()
{
    // 进程型:已在播;M1 暂停/播放语义经 IPC 实现属 M2
    return m_running;
}

bool CXEngineMpvCmd::pause()
{
    // M1 进程型无控制通道(需 IPC,M2);每秒级按钮信号连带错误回执
    return false;
}

bool CXEngineMpvCmd::stop()
{
    if (!m_running)
        return false;
    killProcess();
    emit stopped();
    return true;
}
