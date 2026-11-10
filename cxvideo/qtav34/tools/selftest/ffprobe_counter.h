#ifndef FFPROBE_COUNTER_H
#define FFPROBE_COUNTER_H

#include <qobject.h>

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

#endif // FFPROBE_COUNTER_H
