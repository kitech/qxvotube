#ifndef CXENGMPV_H
#define CXENGMPV_H

#include "cxengine.h"

// libmpv 内嵌内核(detail.md 第 7 节;M1 实装:wid/qt3 + mpv_render_context/qt6)
class CXEngineMpv : public CXEngine {
    Q_OBJECT
public:
    const char* name() const { return "mpv"; }
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

#endif // CXENGMPV_H
