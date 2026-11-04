#ifndef CXENGFFMPEG_H
#define CXENGFFMPEG_H

#include "cxengine.h"

// 自研 FFmpeg 内核(三线程:demux/vthread/athread;M2 实装,见 detail.md 第 4/5 节)
class CXEngineFfmpeg : public CXEngine {
    Q_OBJECT
public:
    const char* name() const { return "ffmpeg"; }
    bool isAvailable() const;

    bool play();
    bool pause();
    bool stop();
    bool seek(long ms);
    bool setRate(double r);
    bool setVolume(int pct);
    bool setMute(bool on);
    long position() const;
    long duration() const;
};

#endif // CXENGFFMPEG_H
