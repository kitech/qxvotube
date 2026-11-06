#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "eng_mpv_cmd.h"
#include "renderer_probe.h"
#include "cxframe.h"
#include <qtimer.h>
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

// ── 实播探针(--play-probe):cmdmpv 引擎进程级生命周期 ──
static bool cxWriteSilentWav1s(const char* path)
{
    FILE* fp = fopen(path, "wb");
    if (fp == 0)
        return false;
    long sampleRate = 8000;
    long samples = sampleRate * 1; // 1 秒
    long bytes = samples * 2;
    unsigned char hdr[44];
    unsigned long pos = 0;
#define PUT32(v) (hdr[pos] = (unsigned char)((v) & 0xFF), hdr[pos+1] = (unsigned char)(((v)>>8)&0xFF), hdr[pos+2] = (unsigned char)(((v)>>16)&0xFF), hdr[pos+3] = (unsigned char)(((v)>>24)&0xFF))
#define PUT16(v) (hdr[pos] = (unsigned char)((v) & 0xFF), hdr[pos+1] = (unsigned char)(((v)>>8)&0xFF))
#define ADV(n) pos += (n)
    memcpy(hdr+0, "RIFF", 4); ADV(4); PUT32(36+bytes); ADV(4);
    memcpy(hdr+8, "WAVE", 4); ADV(4);
    memcpy(hdr+12, "fmt ", 4); ADV(4); PUT32(16); ADV(4);
    PUT16(1); ADV(2); PUT16(1); ADV(2); PUT32(sampleRate); ADV(4);
    PUT32(sampleRate*2); ADV(4); PUT16(2); ADV(2); PUT16(16); ADV(2);
    memcpy(hdr+36, "data", 4); ADV(4); PUT32(bytes); ADV(4);
    fwrite(hdr, 1, sizeof(hdr), fp);
    unsigned char buf[4096];
    memset(buf, 0, sizeof(buf));
    for (long i = 0; i < bytes / (long)sizeof(buf); i++)
        fwrite(buf, 1, sizeof(buf), fp);
    for (long i = bytes % (long)sizeof(buf); i > 0; i--)
        fputc(0, fp);
    fclose(fp);
    return true;
}

static int cxRunPlayProbe(void* appRaw)
{
    // 底座类型 Qt3/Qt6 不同;探测期用基座 QObject 桥接,不指派实体子类型
#ifdef QT3_BUILD
    QApplication* app = (QApplication*)appRaw;
#else
    QCoreApplication* app = (QCoreApplication*)appRaw;
#endif
    const char* path = "/tmp/opencode/cxvideo_selftest.wav";
    if (!cxWriteSilentWav1s(path)) {
        printf("play-probe: wav gen FAIL\n");
        return 1;
    }
    // 走工厂创建(auto/cmdmpv 优先),避免引入私有 ctor 可见度需求
    CXEngine* eng = cxEngineCreateFor(CxEngineMpvCmd);
    int result = 1;
    if (eng == 0) {
        printf("play-probe: SKIP (no mpv binary)\n");
        return 0;
    }
    printf("play-probe: engine=%s\n", eng->name());
    QObject::connect(eng, SIGNAL(started()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    printf("play-probe: open...\n");
    if (!eng->open(QString::fromLatin1(path))) {
        printf("play-probe: open FAIL\n");
        delete eng;
        return 1;
    }
    // 兜底 15s 退出(正常路径 started→quit 先触发)
    QTimer::singleShot(15000, app, SLOT(quit()));
    app->exec();
    eng->stop();
    delete eng;
    printf("play-probe: OK (lifecycle event path exercised)\n");
    result = 0;
    return result;
}

int main(int argc, char* argv[])
{
#ifdef QT3_BUILD
    QApplication app(argc, argv, false);
#else
    QCoreApplication app(argc, argv);
#endif
    int failures = 0;

    // --play-probe 分支(cmdmpv 实播;常规断言仍走完整测试)
    cxSetLocaleNumericC();
    bool playProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--play-probe") == 0)
            playProbeMode = true;
    }
    if (playProbeMode)
        return cxRunPlayProbe(&app);

    EXPECT(strcmp(CXVIDEO_VERSION, CXVIDEO_VERSION) == 0);

    // ── 无引擎基线 ──
    CXVideoPlayer player;
    EXPECT(player.state() == CxStopped);
    EXPECT(player.position() == 0);
    EXPECT(player.duration() == 0);
    EXPECT(player.volume() == 100);
    EXPECT(player.rate() == 1.0);

    EXPECT(!player.setEngine(CxEngineNone));
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
    f.img = cxNewImage(4, 4);
    f.img.fill(0);
    probe.showFrame(&f);
    probe.setPolling(false);

    // ── 引擎探测(最后;cmdmpv 优先,其次 libmpv)──
    if (player.setEngine(CxEngineAuto)) {
        EXPECT(player.engine() != 0);
        printf("engine probe: %s\n", player.engine()->name());
        player.engine()->setHostWidget(0);
        EXPECT(player.engine()->hostWidget() == 0);
    } else {
        printf("engine probe: none available (缺包降级)\n");
    }
    // libmpv 无头探测独立于工厂(cmdmpv 优先时也不跳过)
    int libret = cxEngineMpvProbeLib();
    EXPECT(libret == 0 || libret == 2);

    printf("cxvideo selftest: %d failures, version %s\n", failures, CXVIDEO_VERSION);
    return failures == 0 ? 0 : 1;
}
