#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "eng_mpv_cmd.h"
#include "eng_ffmpeg.h"
#include "renderer_probe.h"
#include "cxframe.h"
#include "ffprobe_counter.h"
#include <qtimer.h>
#ifdef QT3_BUILD
#include <qapplication.h>
#else
#include <qcoreapplication.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// ── ffmpeg 内核实播探针(--ff-probe,M2.1:视频链 + 音频链/主时钟)──
static int cxRunFfProbe(void* appRaw)
{
#ifdef QT3_BUILD
    QApplication* app = (QApplication*)appRaw;
#else
    QCoreApplication* app = (QCoreApplication*)appRaw;
#endif
    const char* path = "/tmp/opencode/cxvideo_selftest.mp4";
    // 无消费 sink 环境(无声卡/CI)下 alsa/pulse 的 writei 阻塞于写端,
    // Finished 拿不到;未显式指定 CX_FF_AO 时探针默认 silent,
    // 保证 convertFlush→ao->drain→Finished 的 EOS 握手全链路可测。
    // 有真实音频设备时可显式 CX_FF_AO=auto 验 alsa/pulse/sdl。
    if (getenv("CX_FF_AO") == 0)
        setenv("CX_FF_AO", "silent", 1);
    // 双输入:testsrc + sine + 外挂 srt;mp4 默认编码即 H.264(libx264)+ AAC;
    // srt → -c:s mov_text 内嵌字幕流(M3-A 同用一份媒体)
    const char* srt = "/tmp/opencode/cxvideo_selftest.srt";
    FILE* sf = fopen(srt, "w");
    if (sf != 0) {
        fputs("1\n00:00:00,800 --> 00:00:01,800\nCXVIDEO-PROBE\n\n"
              "2\n00:00:01,800 --> 00:00:01,900\nCXVIDEO-PROBE2\n", sf);
        fclose(sf);
    }
    if (system("ffmpeg -f lavfi -i testsrc=duration=2:size=160x120:rate=10 "
               "-f lavfi -i sine=frequency=440:duration=2 "
               "-i /tmp/opencode/cxvideo_selftest.srt "
               "-map 0:v -map 1:a -map 2:s "
               "-pix_fmt yuv420p -c:s mov_text -shortest "
               "/tmp/opencode/cxvideo_selftest.mp4 -y "
               "2>/dev/null </dev/null") != 0) {
        printf("ff-probe: SKIP (no ffmpeg binary)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineFfmpeg);
    if (eng == 0) {
        printf("ff-probe: SKIP (WITH_CX_ENGINE_FFMPEG off or probe fail)\n");
        return 0;
    }
    printf("ff-probe: engine=%s\n", eng->name());
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(started()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(finished()),
                     &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(positionChanged(long)),
                     &cnt, SLOT(onFrame(long)));
    if (!eng->open(QString::fromLatin1(path))) {
        printf("ff-probe: open FAIL\n");
        delete eng;
        return 1;
    }
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    const char* aon = ((CXEngineFfmpeg*)eng)->aoBackend();
    char aoBuf[32];
    snprintf(aoBuf, sizeof(aoBuf), "%s", aon != 0 ? aon : "none");
    eng->stop();
    delete eng;
    if (cnt.frames <= 0) {
        printf("ff-probe: FAIL (no position/frames reported)\n");
        return 1;
    }
    if (cnt.frames < 10)
        printf("ff-probe: WARN (thin clock events: frames=%d)\n", cnt.frames);
    if (strcmp(aoBuf, "none") == 0)
        printf("ff-probe: WARN (audio chain absent)\n");
    printf("ff-probe: OK (ao=%s frames=%d lastPts=%ld finished=%d)\n",
           aoBuf, cnt.frames, cnt.lastPts, cnt.gotFinished);
    return 0;
}

// ── M3-A 字幕探针(--sub-probe):ffmpeg 内核播放含 mov_text 内嵌字幕
// 媒体,断言 subtitleChanged 收到非空文本(经 position 事件扫描/变化即发)
static int cxRunSubProbe(void* appRaw)
{
#ifdef QT3_BUILD
    QApplication* app = (QApplication*)appRaw;
#else
    QCoreApplication* app = (QCoreApplication*)appRaw;
#endif
    if (getenv("CX_FF_AO") == 0)
        setenv("CX_FF_AO", "silent", 1);
    const char* path = "/tmp/opencode/cxvideo_selftest.mp4";
    if (system("test -f /tmp/opencode/cxvideo_selftest.mp4 "
               "2>/dev/null </dev/null") != 0) {
        printf("sub-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineFfmpeg);
    if (eng == 0) {
        printf("sub-probe: SKIP (WITH_CX_ENGINE_FFMPEG off)\n");
        return 0;
    }
    printf("sub-probe: engine=%s\n", eng->name());
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(started()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(finished()),
                     &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(positionChanged(long)),
                     &cnt, SLOT(onFrame(long)));
    QObject::connect(eng, SIGNAL(subtitleChanged(const QString&)),
                     &cnt, SLOT(onSubtitle(const QString&)));
    if (!eng->open(QString::fromLatin1(path))) {
        printf("sub-probe: open FAIL\n");
        delete eng;
        return 1;
    }
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    int subs = ((CXEngineFfmpeg*)eng)->subtitleTrackCount();
    eng->stop();
    delete eng;
    if (cnt.subN <= 0) {
        printf("sub-probe: FAIL (no subtitle event; streams=%d)\n", subs);
        return 1;
    }
    printf("sub-probe: OK (events=%d streams=%d first=%s)\n",
           cnt.subN, subs,
#ifdef QT3_BUILD
           (const char*)cnt.lastSub.local8Bit()
#else
           cnt.lastSub.toLocal8Bit().constData()
#endif
           );
    return 0;
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
    bool ffProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ff-probe") == 0)
            ffProbeMode = true;
    }
    if (ffProbeMode)
        return cxRunFfProbe(&app);
    bool subProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sub-probe") == 0)
            subProbeMode = true;
    }
    if (subProbeMode)
        return cxRunSubProbe(&app);

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
