#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "renderer_probe.h"
#include "cxframe.h"
#ifdef QT3_BUILD
#include <qapplication.h>
#else
#include <qcoreapplication.h>
#endif
#include <stdio.h>

#define EXPECT(expr) \
    do { \
        if (!(expr)) { printf("FAIL: %s (line %d)\n", #expr, __LINE__); failures++; } \
    } while (0)

int main(int argc, char* argv[])
{
#ifdef QT3_BUILD
    QApplication app(argc, argv, false);
#else
    QCoreApplication app(argc, argv);
#endif
    int failures = 0;

    EXPECT(strcmp(CXVIDEO_VERSION, CXVIDEO_VERSION) == 0);

    CXVideoPlayer player;
    EXPECT(player.state() == CxStopped);
    EXPECT(player.position() == 0);
    EXPECT(player.duration() == 0);
    EXPECT(player.volume() == 100);
    EXPECT(player.rate() == 1.0);

    // M0:无可用内核,工厂恒 0,setEngine(Auto)==false 且不崩溃
    EXPECT(!player.setEngine(CxEngineAuto));
    EXPECT(player.engine() == 0);
    // open 无引擎走 CxErrEngine 路径
    EXPECT(!player.open("test.mp4"));

    player.setRenderer(0);
    player.setRate(2.0);
    player.setVolume(150); // clamp 100
    EXPECT(player.volume() == 100);
    player.setVolume(-5); // clamp 0
    EXPECT(player.volume() == 0);
    player.setMute(true);
    player.seek(1000);
    player.play();
    player.pause();
    player.stop();
    player.close();

    // probe 帧通路
    CXRendererProbe probe(0);
    probe.setViewport(QRect(0, 0, 4, 4));
    CXVideoFrame f;
    f.ptsMs = 123;
    f.width = 4;
    f.height = 4;
    f.isKey = true;
    f.img = QImage(4, 4, 32);
    f.img.fill(0);
    probe.showFrame(&f);
    probe.setPolling(false);

    printf("cxvideo selftest: %d failures, version %s\n", failures, CXVIDEO_VERSION);
    return failures == 0 ? 0 : 1;
}
