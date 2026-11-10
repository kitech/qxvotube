#ifndef FFPROBE_COUNTER_H
#define FFPROBE_COUNTER_H

#include <qobject.h>
#include <qstring.h>

class CXEngine;
#include "cxengine.h"    // screenshot() 虚方法须完整型
#include "cxrenderer.h"  // M5-A2 EQ 校验(setVideoEq/saveScreenshot)
#include "cxvideo_global.h"

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
    CxShotWorker(CXEngine* e, const char* path = 0)
        : QObject(), eng(e), ok(false),
          shotPath(path != 0 ? QString::fromLatin1(path)
                            : QString::fromLatin1("/tmp/opencode/cxvideo_shot.png")) { }
    CXEngine* eng;
    bool ok;
    QString shotPath;
public slots:
    void take()
    {
        ok = eng->screenshot(shotPath);
    }
};

// M5-A2:GL EQ 校验(基线截图 → setEq(hue 180) → 再截图;探针比对两图)
class CxGlEqWorker : public QObject {
    Q_OBJECT
public:
    CxGlEqWorker(CXVideoRenderer* r)
        : QObject(), renderer(r), baseOk(false), afterOk(false) { }
    CXVideoRenderer* renderer;
    bool baseOk;
    bool afterOk;
public slots:
    void shotBase()
    {
        baseOk = renderer->saveScreenshot(QString::fromLatin1(
            "/tmp/opencode/cx_eq_base.png"));
    }
    void applyEq()
    {
        // 亮度 +60:两内核都应生可见差异(Qt3 CPU LUT / Qt6 shader uniform)
        CXVideoEq eq;
        eq.brightness = 60;
        renderer->setVideoEq(eq);
    }
    void shotAfter()
    {
        afterOk = renderer->saveScreenshot(QString::fromLatin1(
            "/tmp/opencode/cx_eq_hue.png"));
    }
};

#endif // FFPROBE_COUNTER_H
