#ifndef CXENGMPVCMD_H
#define CXENGMPVCMD_H

#include "cxengine.h"

// cmdmpv 内核:mpv 命令行子进程(与 libmpv 内核分引擎实现)
// - 探测顺序在 cxengine 工厂里优先于 libmpv(用户要求)
// - QProcess 生命周期映射:启动→started,退出(播放结束)→finished
// - M1 范围:进程级开/停可控;play 内步进暂停/seek/音量等走后续 IPC 扩展(M2)

#include <qstring.h>

class QProcess;

class CXEngineMpvCmd : public CXEngine {
    Q_OBJECT
public:
    const char* name() const { return "mpv-cmd"; }
    bool isAvailable() const;

    bool open(const QString& url);
    void close();
    bool play();           // 进程型:已在播,恒 true
    bool pause();          // M1 进程型不支持(需 IPC),返回 false
    bool stop();
    bool seek(long /*ms*/) { return false; }
    bool setRate(double) { return false; }
    bool setVolume(int) { return false; }
    bool setMute(bool) { return false; }
    long position() const { return 0; }
    long duration() const { return 0; }

    ~CXEngineMpvCmd();

private slots:
    void procFinished();

private:
    CXEngineMpvCmd();
    void killProcess();
    friend CXEngine* cxEngineCreateDefault();
    friend CXEngine* cxEngineCreateFor(CxEngineType type);

    QProcess* p_proc; // 仅在 cpp 中析构(头部此前定义)
    bool m_running;
    QString m_binPath;
};

#endif // CXENGMPVCMD_H
