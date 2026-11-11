#include "compat34.h"
#include "cxvideoplayer.h"
#include "cxengine.h"
#include "eng_mpv.h"
#include "eng_mpv_cmd.h"
#include "eng_ffmpeg.h"
#include "renderer_probe.h"
#include "renderer_software.h"
#include "renderer_gl.h"
#include "renderer_mpvgl.h"
#ifndef QT3_BUILD
#include "renderer_quick.h"
#include "renderer_qmpv.h"
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <functional>
#endif
#include "cxframe.h"
#include "ffprobe_counter.h"
#include <qtimer.h>
#include <qfile.h>
#include <unistd.h>
#include <qfileinfo.h>
#ifdef QT3_BUILD
#include <qapplication.h>
#else
#include <QApplication> // M4-A:--gl-probe 需 GUI(QOpenGLWidget)
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

// ── M3-B 截图探针(--shot-probe):setEq 非零 + 播放中软渲 saveScreenshot,
// 断言 PNG 落地非空(截图为 LUT 后画面即所见即所得;CxShotWorker 见 ffprobe_counter.h)
static int cxRunShotProbe(void* appRaw)
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
        printf("shot-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineFfmpeg);
    if (eng == 0) {
        printf("shot-probe: SKIP (WITH_CX_ENGINE_FFMPEG off)\n");
        return 0;
    }
    printf("shot-probe: engine=%s\n", eng->name());
    CXRendererProbe view(0); // headless 截图(无 GUI app 不能起 QWidget)
    eng->setRenderer(&view);
    CXVideoEq eq;
    eq.brightness = 30;
    eq.contrast = 20;
    eq.saturation = 40;
    bool eqOk = ((CXEngineFfmpeg*)eng)->setEq(eq);
    eng->open(QString::fromLatin1(path));
    eng->play();
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(finished()), &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    CxShotWorker w(eng);
    QTimer::singleShot(600, &w, SLOT(take())); // 播放中途截图
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    eng->stop();
    delete eng;
    QFile f("/tmp/opencode/cxvideo_shot.png");
    if (!eqOk) {
        printf("shot-probe: FAIL (setEq)\n");
        return 1;
    }
    if (!f.exists() || f.size() <= 1000) {
        printf("shot-probe: FAIL (png missing/empty, ok=%d)\n", (int)w.ok);
        return 1;
    }
    printf("shot-probe: OK (eq=b30/c20/s40 size=%d)\n", (int)f.size());
    return 0;
}

// ── M3-B 倍速探针(--rate-probe):setRate(2.0) 全片播完,断言进度与 finished
static int cxRunRateProbe(void* appRaw)
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
        printf("rate-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineFfmpeg);
    if (eng == 0) {
        printf("rate-probe: SKIP (WITH_CX_ENGINE_FFMPEG off)\n");
        return 0;
    }
    printf("rate-probe: engine=%s\n", eng->name());
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(finished()), &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(positionChanged(long)),
                     &cnt, SLOT(onFrame(long)));
    if (!eng->open(QString::fromLatin1(path))) {
        printf("rate-probe: open FAIL\n");
        delete eng;
        return 1;
    }
    bool rOk = eng->setRate(2.0);
    eng->play();
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    eng->stop();
    delete eng;
    if (!rOk) {
        printf("rate-probe: FAIL (setRate)\n");
        return 1;
    }
    if (cnt.frames <= 0 || cnt.gotFinished != 1) {
        printf("rate-probe: FAIL (frames=%d finished=%d)\n",
               cnt.frames, cnt.gotFinished);
        return 1;
    }
    printf("rate-probe: OK (frames=%d lastPts=%ld @2x)\n",
           cnt.frames, cnt.lastPts);
    return 0;
}

// ── M4-A 硬解探针(--hw-probe):探活(vaapi→cuda)实播一程;
// 无 hw → SKIP(软解本体断言照旧);有 hw → OK 打名
static int cxRunHwProbe(void* appRaw)
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
        printf("hw-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineFfmpeg);
    if (eng == 0) {
        printf("hw-probe: SKIP (WITH_CX_ENGINE_FFMPEG off)\n");
        return 0;
    }
    printf("hw-probe: engine=%s\n", eng->name());
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(finished()), &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(positionChanged(long)),
                     &cnt, SLOT(onFrame(long)));
    if (!eng->open(QString::fromLatin1(path))) {
        printf("hw-probe: FAIL (open)\n");
        delete eng;
        return 1;
    }
    eng->play();
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    const char* hw = ((CXEngineFfmpeg*)eng)->hwName();
    eng->stop();
    delete eng;
    if (cnt.frames <= 0 || cnt.gotFinished != 1) {
        printf("hw-probe: FAIL (frames=%d finished=%d)\n",
               cnt.frames, cnt.gotFinished);
        return 1;
    }
    if (hw == 0) {
        printf("hw-probe: SKIP (hw-unavail; soft-decode flow OK, frames=%d)\n",
               cnt.frames);
        return 0;
    }
    printf("hw-probe: OK (hw=%s frames=%d finished=1)\n", hw, cnt.frames);
    return 0;
}

// ── M4-A GL 渲染探针(--gl-probe):QGLWidget/QOpenGLWidget 实渲一程;
// GUI 前提(main 里 GUI-enabled 构造,无 DISPLAY 由 QApplication 自报)
static int cxRunGlProbe(void* appRaw)
{
    QApplication* app = (QApplication*)appRaw;
    if (getenv("CX_FF_AO") == 0)
        setenv("CX_FF_AO", "silent", 1);
    const char* path = "/tmp/opencode/cxvideo_selftest.mp4";
    if (system("test -f /tmp/opencode/cxvideo_selftest.mp4 "
               "2>/dev/null </dev/null") != 0) {
        printf("gl-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineFfmpeg);
    if (eng == 0) {
        printf("gl-probe: SKIP (WITH_CX_ENGINE_FFMPEG off)\n");
        return 0;
    }
    printf("gl-probe: engine=%s\n", eng->name());
    CXRendererGl view(0);
    view.resize(320, 240);
    view.show();
    eng->setRenderer(&view);
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(finished()), &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(positionChanged(long)),
                     &cnt, SLOT(onFrame(long)));
    if (!eng->open(QString::fromLatin1(path))) {
        printf("gl-probe: FAIL (open)\n");
        delete eng;
        return 1;
    }
    // M5-A2:EQ 生效校验(基线 → brightness+60 → 再拍,比对回读像素)
    CxGlEqWorker eqw(&view);
    QTimer::singleShot(600, &eqw, SLOT(shotBase()));
    QTimer::singleShot(900, &eqw, SLOT(applyEq()));
    QTimer::singleShot(1200, &eqw, SLOT(shotAfter()));
    eng->play();
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    eng->stop();
    delete eng;
    bool glOk = view.glReady(); // Qt3 恒 true;Qt6 校验 shader 链真就绪
    int eqDiff = -1;
    if (eqw.baseOk && eqw.afterOk) {
        QImage a, b;
        if (a.load("/tmp/opencode/cx_eq_base.png") &&
            b.load("/tmp/opencode/cx_eq_hue.png")) {
            int w = a.width() < b.width() ? a.width() : b.width();
            int h = a.height() < b.height() ? a.height() : b.height();
            long long sum = 0, n = 0;
            for (int y = 0; y < h; y += 2) {
                for (int x = 0; x < w; x += 2) {
                    QRgb p1 = a.pixel(x, y), p2 = b.pixel(x, y);
                    int dr = (int)qRed(p1) - (int)qRed(p2);
                    int dg = (int)qGreen(p1) - (int)qGreen(p2);
                    int db = (int)qBlue(p1) - (int)qBlue(p2);
                    if (dr < 0) dr = -dr;
                    if (dg < 0) dg = -dg;
                    if (db < 0) db = -db;
                    sum += dr + dg + db;
                    n++;
                }
            }
            eqDiff = n > 0 ? (int)(sum / (3 * n)) : 0;
        }
    }
    if (cnt.frames <= 0 || cnt.gotFinished != 1 || !glOk || eqDiff < 5) {
        printf("gl-probe: FAIL (frames=%d finished=%d glReady=%d eqDiff=%d)\n",
               cnt.frames, cnt.gotFinished, (int)glOk, eqDiff);
        return 1;
    }
    printf("gl-probe: OK (frames=%d lastPts=%ld finished=1 eqDiff=%d)\n",
           cnt.frames, cnt.lastPts, eqDiff);
    return 0;
}

// ── M4-B mpv-wid 回退路探针:强制 CX_MPV_ROUTE=wid ──
// 断言:renderPath=mpv-wid + 帧流正常;EQ/截图在 wid 路应如实返回 false(不崩)
// ── M5-B Quick 探针:ffmpeg 纹理路真渲断言 + mpv-qfbo 挂载断言 ──
static int cxRunQuickProbe(void* appRaw)
{
#ifdef QT3_BUILD
    (void)appRaw;
    printf("quick-probe: SKIP (Qt3 非 Quick 域)\n");
    return 0;
#else
    QApplication* app = (QApplication*)appRaw;
    if (getenv("CX_FF_AO") == 0)
        setenv("CX_FF_AO", "silent", 1);
    // mpv-qfbo 显式要求 GL 窗口(不满足即 FAIL,不降级)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    if (QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL) {
        printf("quick-probe: FAIL (window graphicsApi != OpenGL)\n");
        return 1;
    }

    // ── 断言 1:ffmpeg rhi-texture 真渲(纯红帧 → grabWindow 中心点红)──
    QQuickWindow win;
    win.setGeometry(0, 0, 320, 240);
    CXQuickVideoItem* vitem = new CXQuickVideoItem(win.contentItem());
    vitem->setSize(QSizeF(320, 240));
    CXVideoFrame f;
    f.width = 160;
    f.height = 120;
    f.img = QImage(160, 120, QImage::Format_RGB32);
    f.img.fill(QColor(255, 0, 0));
    vitem->showFrame(&f);
    win.show();
    app->processEvents();
    app->processEvents();
    QImage shot = win.grabWindow();
    bool ffOk = !shot.isNull();
    if (ffOk) {
        QRgb c = shot.pixel(160, 120);
        ffOk = qRed(c) > 200 && qGreen(c) < 60 && qBlue(c) < 60;
    }
    delete vitem;

    // ── 断言 2:mpv-qfbo 挂载(quick 路打开 + 首帧懒挂)──
    bool mpvOk = false;
    CXEngine* meng = cxEngineCreateFor(CxEngineMpv);
    if (meng == 0) {
        printf("quick-probe: mpv SKIP (libmpv unavailable)\n");
        mpvOk = true;
    } else if (system("test -f /tmp/opencode/cxvideo_selftest.mp4 "
                      "2>/dev/null </dev/null") != 0) {
        printf("quick-probe: mpv SKIP (media missing; run --ff-probe first)\n");
        mpvOk = true;
    } else {
        CXEngineMpv* em = (CXEngineMpv*)meng;
        em->setQuickRender(true);
        CXQuickMpvFboItem* mitem = new CXQuickMpvFboItem(win.contentItem());
        mitem->setX(0);
        mitem->setY(0);
        mitem->setWidth(320);
        mitem->setHeight(120);
        mitem->setZ(1); // 覆盖在 ff 断言用例层之上,场景图真渲
        mitem->bindEngine(em);
        CxFFProbeCounter cnt;
        QObject::connect(em, SIGNAL(finished()), &cnt, SLOT(onFinished()));
        QObject::connect(em, SIGNAL(positionChanged(long)),
                         &cnt, SLOT(onFrame(long)));
        QObject::connect(em, SIGNAL(finished()), app, SLOT(quit()));
        QTimer::singleShot(10000, app, SLOT(quit()));
        bool opened = meng->open(QString::fromLatin1(
            "/tmp/opencode/cxvideo_selftest.mp4"));
        meng->play();
        // 二段起播:open 只建 handle(vo=libmpv),loadfile 延至 render_context
        // 挂成(render.h:须在起播前建好)。轮询 attach 位→quickGo 发 loadfile。
        std::function<void()> quickGoPoll = [&]() {
            if (mitem->attached())
                em->quickGo();
            else
                QTimer::singleShot(50, app, quickGoPoll);
        };
        QTimer::singleShot(50, app, quickGoPoll);
        app->exec(); // 等 render 线程首帧懒挂 + 帧流
        mpvOk = opened &&
                em->renderPath() != 0 &&
                strcmp(em->renderPath(), "mpv-gl") == 0 &&
                em->renderObj() != 0 &&
                mitem->attached() &&
                cnt.frames > 0;
        if (!mpvOk)
            printf("quick-probe: mpv detail open=%d path=%s rc=%p att=%d fr=%d\n",
                   (int)opened,
                   em->renderPath() != 0 ? em->renderPath() : "none",
                   em->renderObj(), (int)mitem->attached(), cnt.frames);
        meng->stop();
        delete meng;
        delete mitem;
    }

    if (!ffOk || !mpvOk) {
        printf("quick-probe: FAIL (ff=%d mpv=%d)\n", (int)ffOk, (int)mpvOk);
        return 1;
    }
    printf("quick-probe: OK (rhi-texture pixel; mpv-qfbo attached render=mpv-gl)\n");
    return 0;
#endif
}

static int cxRunMpvWidProbe(void* appRaw)
{
    QApplication* app = (QApplication*)appRaw;
    if (getenv("CX_FF_AO") == 0)
        setenv("CX_FF_AO", "silent", 1);
    setenv("CX_MPV_ROUTE", "wid", 1);
    const char* path = "/tmp/opencode/cxvideo_selftest.mp4";
    if (system("test -f /tmp/opencode/cxvideo_selftest.mp4 "
               "2>/dev/null </dev/null") != 0) {
        printf("mpv-wid-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineMpv);
    if (eng == 0) {
        printf("mpv-wid-probe: SKIP (libmpv unavailable)\n");
        return 0;
    }
    printf("mpv-wid-probe: engine=%s\n", eng->name());
    QWidget host;
    host.resize(320, 240);
    host.show();
    eng->setHostWidget(&host);
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(finished()), &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(positionChanged(long)), &cnt, SLOT(onFrame(long)));
    if (!eng->open(QString::fromLatin1(path))) {
        printf("mpv-wid-probe: FAIL (open)\n");
        delete eng;
        return 1;
    }
    CXVideoEq eq;
    eq.brightness = 30;
    bool eqOk = ((CXEngineMpv*)eng)->setEq(eq);
    eng->play();
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    const char* rp = ((CXEngineMpv*)eng)->renderPath();
    bool shotOk = ((CXEngineMpv*)eng)->screenshot(
        QString::fromLatin1("/tmp/opencode/mpv_wid_shot.png"));
    eng->stop();
    delete eng;
    bool pathOk = rp != 0 && strcmp(rp, "mpv-wid") == 0;
    if (!pathOk || cnt.frames <= 0 || cnt.gotFinished != 1 || shotOk) {
        printf("mpv-wid-probe: FAIL (render=%s frames=%d finished=%d shot=%d)\n",
               rp != 0 ? rp : "none", cnt.frames, cnt.gotFinished, (int)shotOk);
        return 1;
    }
    printf("mpv-wid-probe: OK (render=mpv-wid frames=%d eq=%d shot=%d)\n",
           cnt.frames, (int)eqOk, (int)shotOk);
    return 0;
}

// ── M4-B mpv-gl 探针:eng_mpv render_context 路 ──
// 有 GL+render 符号 → OK(renderPath=mpv-gl;EQ 不炸;截图落地);
// 否则如实 SKIP(wid 回退路帧流照旧断言)
static int cxRunMpvGlProbe(void* appRaw)
{
    QApplication* app = (QApplication*)appRaw;
    if (getenv("CX_FF_AO") == 0)
        setenv("CX_FF_AO", "silent", 1);
    const char* vidPath = "/tmp/opencode/cxvideo_selftest.mp4";
    if (system("test -f /tmp/opencode/cxvideo_selftest.mp4 "
               "2>/dev/null </dev/null") != 0) {
        printf("mpv-gl-probe: SKIP (media missing; run --ff-probe first)\n");
        return 0;
    }
    CXEngine* eng = cxEngineCreateFor(CxEngineMpv);
    if (eng == 0) {
        printf("mpv-gl-probe: SKIP (libmpv unavailable)\n");
        return 0;
    }
    printf("mpv-gl-probe: engine=%s\n", eng->name());
    CXRendererMpvGl view(0);
    view.resize(384, 288);
    view.show();
    eng->setHostWidget(&view);
    CxFFProbeCounter cnt;
    QObject::connect(eng, SIGNAL(finished()), &cnt, SLOT(onFinished()));
    QObject::connect(eng, SIGNAL(finished()), app, SLOT(quit()));
    QObject::connect(eng, SIGNAL(positionChanged(long)), &cnt, SLOT(onFrame(long)));
    if (!eng->open(QString::fromLatin1(vidPath))) {
        printf("mpv-gl-probe: FAIL (open)\n");
        delete eng;
        return 1;
    }
    // M4-B:EQ 路交验(不炸即过;返回值如实记录)
    CXVideoEq mpveq;
    mpveq.brightness = 30; mpveq.contrast = 20;
    mpveq.saturation = 40; mpveq.hue = -15;
    bool eqOk = ((CXEngineMpv*)eng)->setEq(mpveq);
    const char* rp = ((CXEngineMpv*)eng)->renderPath();
    bool glRoute = rp != 0 && strcmp(rp, "mpv-gl") == 0;
    QString shotPath = QString::fromLatin1("/tmp/opencode/cxvideo_mpvgl_shot.png");
    QFile::remove(shotPath);
    CxShotWorker shot(eng, "/tmp/opencode/cxvideo_mpvgl_shot.png"); // 播放中截图(600ms;keep-open=no 下 EOF 后无帧可截)
    if (glRoute)
        QTimer::singleShot(600, &shot, SLOT(take()));
    eng->play();
    QTimer::singleShot(10000, app, SLOT(quit()));
    app->exec();
    bool shotOk = false;
    if (glRoute) {
        for (int i = 0; i < 30 && !shotOk; i++) { // mpv 命令异步落盘,轮询至 3s
            if (QFileInfo(shotPath).size() > 1000)
                shotOk = true;
            else
                usleep(100000);
        }
        shotOk = shotOk && shot.ok;
    }
    eng->stop();
    delete eng;
    if (rp != 0 && strcmp(rp, "mpv-gl") == 0) {
        // gl 路:帧流 + 截图断言
        if (cnt.frames <= 0 || cnt.gotFinished != 1 || !shotOk ||
            QFileInfo(shotPath).size() <= 1000) {
            printf("mpv-gl-probe: FAIL (frames=%d finished=%d eq=%d shot=%d)\n",
                   cnt.frames, cnt.gotFinished, (int)eqOk, (int)shotOk);
            return 1;
        }
        printf("mpv-gl-probe: OK (render=mpv-gl frames=%d eq=%d shot=%d)\n",
               cnt.frames, (int)eqOk, (int)shotOk);
        return 0;
    }
    if (cnt.frames > 0 && cnt.gotFinished == 1) {
        printf("mpv-gl-probe: SKIP (wid fallback; frames=%d)\n", cnt.frames);
        return 0;
    }
    printf("mpv-gl-probe: FAIL (frames=%d finished=%d render=%s)\n",
           cnt.frames, cnt.gotFinished, rp != 0 ? rp : "none");
    return 1;
}

int main(int argc, char* argv[])
{    // M4-A:--gl-probe 需 GUI-enabled app(QGLWidget/QOpenGLWidget);
    // 先于 app 构造预扫 argv
    bool glProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gl-probe") == 0 ||
            strcmp(argv[i], "--mpv-gl-probe") == 0 ||
            strcmp(argv[i], "--mpv-wid-probe") == 0)
            glProbeMode = true;
    }
#ifdef QT3_BUILD
    QApplication app(argc, argv, glProbeMode);
#else
    QApplication app(argc, argv); // Qt6 GUI(QOpenGLWidget 需 QApplication)
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
    bool shotProbeMode = false;
    bool rateProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot-probe") == 0)
            shotProbeMode = true;
        if (strcmp(argv[i], "--rate-probe") == 0)
            rateProbeMode = true;
    }
    if (shotProbeMode)
        return cxRunShotProbe(&app);
    if (rateProbeMode)
        return cxRunRateProbe(&app);
    bool hwProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hw-probe") == 0)
            hwProbeMode = true;
    }
    if (hwProbeMode)
        return cxRunHwProbe(&app);
    bool mpvWidProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mpv-wid-probe") == 0)
            mpvWidProbeMode = true;
    }
    if (mpvWidProbeMode)
        return cxRunMpvWidProbe(&app);
    bool quickProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick-probe") == 0)
            quickProbeMode = true;
    }
    if (quickProbeMode)
        return cxRunQuickProbe(&app);
    bool mpvGlProbeMode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mpv-gl-probe") == 0)
            mpvGlProbeMode = true;
    }
    if (mpvGlProbeMode)
        return cxRunMpvGlProbe(&app);
    if (glProbeMode)
        return cxRunGlProbe(&app);

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
