#ifndef FFPROBE_COUNTER_H
#define FFPROBE_COUNTER_H

#include <qobject.h>
#include <qstring.h>

class CXEngine;
#include "cxengine.h" // screenshot() 虚方法须完整型

// --ff-probe 帧到计数器(连接 engine positionChanged / finished / subtitleChanged)
class CxFFProbeCounter : public QObject {
    Q_OBJECT
public:
    CxFFProbeCounter()
        : QObject(), frames(0), lastPts(-1), gotFinished(0), subN(0) { }

    int frames;
    long lastPts;
    int gotFinished;
    int subN;
    QString lastSub;

public slots:
    void onFrame(long pts)
    {
        frames++;
        lastPts = pts;
    }
    void onFinished() { gotFinished = 1; }
    void onSubtitle(const QString& t)
    {
        if (!t.isEmpty()) {
            subN++;
            lastSub = t;
        }
    }
};

// --shot-probe 截图中转(QTimer::singleShot 命中;须在头文件供 moc)
class CxShotWorker : public QObject {
    Q_OBJECT
public:
    CxShotWorker(CXEngine* e) : QObject(), eng(e), ok(false) { }
    CXEngine* eng;
    bool ok;
public slots:
    void take()
    {
        ok = eng->screenshot(QString::fromLatin1(
            "/tmp/opencode/cxvideo_shot.png"));
    }
};

#endif // FFPROBE_COUNTER_H
