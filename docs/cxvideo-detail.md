# cxvideo 详细设计

对应 `cxvideo-plan.md` 的实施细节。目标位置:本仓库 `cxvideo/qtav34/`;本文档为骨架任务与实现期代码契约。vendored 参照:`cxvideo/qtav56/`(原版快照,已去 qml)、`cxvideo/qtavqml/`(nejatafshar QML fork 快照)—— 溯源与差异统计见 `cxvideo/README.md`。

## 1. 目录结构

```
cxvideo/qtav34/          (自研主线;qtav56/qtavqml 为同目录下 vendored 快照)
  cxvideo.pri            qmake 集成(已实现,qlcomp compat34 复用)
  CMakeLists.txt       Qt5.15+/Qt6 可选
  src/
    cxvideo_global.h   版本/导出宏/compat 引入
    cxthread.h/.cpp         CXThread 壳(QThread 包装/sleep/原子标志)
    cxvideoplayer.h/.cpp    CXVideoPlayer 编排与状态机
    cxengine.h              CXEngine 抽象+工厂+探测
    eng_mpv.h/.cpp          CXEngineMpv(libmpv C-API 内嵌)
    eng_ffmpeg.h/.cpp       CXEngineFfmpeg 线程组外壳
    ff_demux.h/.cpp         CXDemuxThread(+avformat 封装)
    ff_vthread.h/.cpp       CXVideoThread(解码+丢帧+送渲)
    ff_athread.h/.cpp       CXAudioThread(解码+重采样+输出+主时钟)
    ff_clock.h/.cpp         CXClock
    ff_decoder.h/.cpp       CXDecoder(avcodec;M4: VAAPI/DXVA2 探测)
    ff_audioout.h/.cpp      CXAudioOut 接口
    ff_audioout_al.h/.cpp   OpenALSoft 后端
    ff_audioout_pa.h/.cpp   PulseAudio 后端(条件编译)
    cxframe.h               CXVideoFrame/CXAudioFrame
    cxrenderer.h            CXVideoRenderer 接口
    renderer_software.h/.cpp QImage→paintEvent(全 Qt)
    renderer_x11.h/.cpp     X11 直渲 winId(无 GL 兜底)
    renderer_gl.h/.cpp      条件编译: Qt4-QGLWidget / Qt5.4+QOpenGLWidget / Qt6-QOpenGLWidget
    renderer_probe.h/.cpp   QTimer→QImage 帧回调(Quick/测试)
  tools/
    cxvideo_demo/           独立播放器 demo(qt3/qt4/qt6 三套 .pro 变体)
    selftest/               无头自测(纯 main,不依赖 Qt GUI 事件的时序/帧序校验)
  docs/
    README.md               该仓库内的用户文档/进度表/接入指引
```

## 2. QtAV→cxvideo API 名字映射(QtAV 风格照搬)

| QtAV | cxvideo | 备注 |
|---|---|---|
| AVPlayer | CXVideoPlayer | API 同形:load/play/pause/stop/seek |
| VideoRenderer | CXVideoRenderer | 接口方法 showFrame(frame) |
| setRenderer / renderer() | setRenderer / renderer() | 同名 |
| play(file)/pause/stop | play(QString)/pause/stop | Qt3 下 QString 正常可用 |
| seek(ms) | seek(qint64 ms) | |
| duration()/position() | duration()/position() | 毫秒,qint64 |
| setSpeed / speed | setRate / rate | |
| setVolume / volume(0~1) | setVolume(pct) / volume | 0~100,与 qlvotube PlayerBar 对齐 |
| state() / Playing 等状态 | state() / enum CXVideoState { Stopped, Loading, Paused, Playing } | |
| waitForState 等 | 同名 | |
| 视频 EQ(亮度/对比度/饱和度/色度) | setEq(CXVideoEq) | 软/GL 两路实现(M3) |
| track 选择/字幕切换 | setAudioTrack(int) / setSubtitleTrack(int) / setSubtitleFile(QString) | 两个内核分别映射(M3) |
| QML MediaPlayer 兼容 API | 不做(QML 层不承诺) | qskvotube 走 RendererProbe |

### 2.1 信号(Qt3 字符串签名 + Qt5/6 成员指针双兼容)

- Qt3 必须字符串签名:`connect(p, SIGNAL(positionChanged(long)), r, SLOT(onPos(long)))`;Qt5/6 侧照常可加成员指针 connect,同一信号集两侧通用。
- 注意 qint64 在 Qt3 moc 下 connect 字符串类型问题:Qt3 的 SIGNAL 宏串里直接写 `qint64` 时由 compat34 里 `#define qint64 long` 兜底(qlappcom 已有先例,按其约定统一)。
- 统一信号集:
```
started() / paused() / stopped() / finished()
mediaStatusChanged(int)
positionChanged(qint64) / durationChanged(qint64)
seekFinished(qint64)
error(int code, QString msg)
videoEqChanged()
```
- 帧外发(RendererProbe 通道)独立于上述集:`frameReady(const CXVideoFrame*)`——以指针传递,避免 QImage 深拷贝;帧由内部帧池出借、回调方只读,勿持有越过下一帧。

## 3. 类概览(职责/可见性)

```
CXVideoPlayer : QObject
  属性: source(QString) mute(bool) rate(double) volume(int)
        state(CXVideoState) engine(CxEngineType 枚举)
  约定: 全 API 仅 UI 线程调用;内核线程→UI 用 QCustomEvent(Qt3 兼容)回投,
        复用 qlappcom EventPoller 的"自定 EventType + customEvent 分发"实践。
CXEngine : QObject(纯虚)
  + attach(CXVideoRenderer*, CXVideoWindowHost*)   // 渲染器+宿主(含 winId)
  + open(source) / close()
  + play() / pause() / stop() / seek(ms)
  + setRate / setVolume / setMute
  + setAudioTrack(int) / setSubtitleTrack(int) / setSubtitleFile(QString)
  + 只读: duration / position / audioTrackCount / subtitleTrackCount
  信号: started/paused/stopped/finished/positionChanged/seekFinished/error
CXEngineMpv     : CXEngine     // libmpv: mpv_handle(+Qt4/6 GL 时 mpv_render_context)
CXEngineFfmpeg  : CXEngine     // 三线程组外壳,委托 ff_* 内部件
CXVideoRenderer : 独立接口(不继承 QWidget)
  + virtual void showFrame(const CXVideoFrame*) = 0;   // 仅 UI 线程被调
  + virtual void setViewport(const QRect& contentRect) // letterbox 定位
CXVideoWindowHost : 接口(宿主 QWidget: winId()/size 变更通知)
```

## 4. 线程模型与生命周期

- 三线程(原型=QtAV):CXDemuxThread / CXVideoThread / CXAudioThread;启动/close 全由 CXEngineFfmpeg 在 UI 线程编排;stop 流程 = 置停止位 → 唤醒全部阻塞 take() → join → 清队列 → free(AVCodecContext/AVFormatContext 独立收放)。
- seek 时序(复刻 QtAV 握手):
```
UI seek(t):
  demuxThread.seek(t)      // 清包队列; avformat_seek_file(t, BACKWARD)
  vthread.requestSeek(t)   // render_pts0 = t, sync_id = ++global
  athread.requestSeek(t)   // 同 sync_id
  [athread] 丢弃 pts<t 的包,首帧渲染时 clock->updateValue(t)
  [vthread] 丢弃 pts<t 的包,首个显示帧时 clock->syncDone(sync_id)
  → seekFinished(t) 由 athread 发出(事件回投 UI)
```
- 关闭顺序与 QtAV AudioThread 出口一致:sync_id 作废 → 先清队列再退 run,避免 join 死锁。

## 5. CXEngineFfmpeg 解码管线(逐阶段)

帧/包定义:
```
CXVideoFrame { pts(ms), w, h, pixfmt(enum,首版统一转 32-bit RGB 交渲染),
               data(帧池出借), isKey }
CXAudioFrame { pts(ms), fmt(s16/s32/f32), nch, rate, bytes }
```

**CXDemuxThread::run()**:
```
while(!stop):
    if(两条包队列均满): 条件等待(被消费唤醒); continue
    pkt = av_read_frame()
    if(!pkt): 向视频/音频队列各投一个 EOF 标记包; break
    按流类型分别 push(videoQ/audioQ)/丢弃(字幕流转存另用,M3)
```
- 容量:videoQ 256 包 / audioQ 512 包(固定,可配置);take() 阻塞取,支持"停止位唤醒"。

**CXVideoThread::run()**:
```
render_pts0 = -1                    // seek 握手
while(!stop):
    pkt = videoQ.take(stop)
    if(!pkt.valid) continue                       // EOF/停止
    if(seekPts>=0 && pkt.pts < render_pts0) drop; continue
    frame = decoder.decode(pkt)                   // 解码循环直到出帧或 pkt 耗尽
    if(!frame) { continue } if(frame.pts < render_pts0) { drop; continue }
    delay = frame 时长(ms) - (clock.value() - 上次音频时钟基准)
    waitAndCheck(delay, stop)                    // 条件等待,可被 seek/停止打断
    if(clock 有新增 seek) continue
    renderer->showFrame(frame, 经事件投回 UI 线程调用) → 回收帧池;该帧触发 syncDone(sync_id)
    解码器结束(EOF): 投 finished, break
```
- 送渲为事件投递:UI 线程 paint;不直接在解码线程画(避非 UI 绘图/竞态)。
- 格式转换: fixed pipeline `sws_scale → QImage(RGB32)`,sws context 按 (w,h PixFmt) 缓存一次(QTAV "preferredPixelFormat 转一次" 思想);M4 硬解则绕过 sws。

**CXAudioThread::run()**(主时钟):
```
while(!stop):
    pkt = audioQ.take(stop)
    if(!pkt.valid) continue
    if(seekPts>=0 && pkt.pts < render_pts0) drop; continue
    frame = decoder.decode(pkt)                  // 先重采样到 AO 期望 fmt/rate/ch
    ao.play(chunk, frame.pts)
    clock.updateValue(frame.pts)                 // 主时钟推进
    (EOF: 投 finished; 队列清空后 AO 排空再退)
```
- 与 QtAV AudioThread 对齐的五个要点:主时钟在音频、waitAndCheck 后可提前丢帧、AO 时间戳做校准、EOF 后先排空再退出、按 `ao.bufferSize()` 分 chunk 喂。

**CXClock**:`value()/updateValue(t)/waitAndCheck(delay, stopFlag)/speed(rate)`;实现 = QMutex + QWaitCondition(Qt3 兼容);速度影响 delay 换算。

## 6. 渲染器逐个实现说明

| 渲染器 | 机制 | Qt 适用 | 备注 |
|---|---|---|---|
| CXRendererSoftware | showFrame → 换入三缓冲 QImage → `update()`;paintEvent 内 `p.drawImage(letterboxRect)`(黑边) | 全 Qt,**Qt3 首选** | Qt3 里 YUV→QImage(RGB32) 沿用第 5 节 sws;paint 时无需再转 |
| CXRendererX11 | XImage/MIT-SHM 直渲到 winId(XCreateImage/XPutImage),YUV→RGB 前置 sws | 全 Qt(无 GL 环境的兜底;Qt3 无 QOpenGLWidget 的主力 GL 替代) | 宿主提供 WId;进程需 XInitThreads |
| CXRendererGL | Qt4: QGLWidget;Qt5.4+: QOpenGLWidget;首版逐帧 `glTexSubImage2D`(不做 PBO),shader yuv2rgb/EQ 属 M4 加成 | Qt4/5/6 条件编译 | Qt3 跳过 |
| CXRendererProbe | 无窗口:最近帧以 30ms 节流经 frameReady 信号外发 QImage | 全 Qt | qskvotube(QSkinny/Quick)M5 独占入口;自测比对亦用 |

## 7. CXEngineMpv(libmpv)集成细节

- `mpv_create()` → 设定 `vo=libmpv`;音频直走系统后端(依探测选 `ao=pulse` 或 `ao=alsa`);渲染渠道按宿主能力:
  - 无 GL(含 Qt3):`mpv_set_option wid=<X11 winId>` → mpv 自建 X11 子窗;宿主 QWidget 管生命周期,resize 由子窗几何自动跟随,无须额外处理。
  - Qt4/5.4+/Qt6 有 GL:mpv_render_context(RENDER_API=opengl)+ QOpenGLWidget paintGL 中 `mpv_render_context_render(fbo=0, flip=y)`。
  - Quick(qskvotube,M5):短线路先用 RendererProbe 的 QImage 回调;`mpv_render_context` 直接渲到内存 FBO 的路径 M5 里实测再定。
- 属性/命令映射:时长/进度 = `mpv_observe_property(time-pos / duration)`;seek = `seek <t> absolute`;倍速 = `set speed`;轨选 = `set aid/sid`;外挂字幕 = `sub-add <path>`;错误 = `mpv_event_end_file.reason`(甄别 error vs eof)。
- 事件泵:自备轻线程 `mpv_wait_event(-1)` → 转成 QCustomEvent 回投(cxthread 完成)。

### 7.1 M1 实装记录(2026-11,Qt3/Qt6 双绿)

- **双引擎分轨**(用户约定):libmpv 与 cmdmpv **分开实现**;
  - `CXEngineMpvCmd`(eng_mpv_cmd.{h,cpp}):mpv 命令行子进程,QProcess 生命周期映射 started/finished;M1 只承诺进程级开/停(play/pause/seek 交 M2 IPC 扩展)。
  - `CXEngineMpv`(eng_mpv.{h,cpp}):libmpv dlopen 精细控制(wid 内嵌、observe、命令)。
  - **探测顺序:cmdmpv 优先**(cxengine.cpp);
  - 工厂:`cxEngineCreateFor(type)`(按枚举)/`cxEngineCreateDefault()`(cmdmpv→libmpv→ffmpeg M2)。
- **libmpv 缺包降级**:dlopen 探测,不链 -lmpv;`isAvailable()` false 即跳过(qlcomp 兼容面不变)。
- **locale 硬需求**:libmpv `check_locale()` 要求 `LC_NUMERIC ∈ {C, C.UTF-8}`,否则 `mpv_create()` 返回 NULL(mpv player/main.c;Qt 官方文档亦明示 QApp 构造会 `setlocale(LC_ALL,"")` 抢走);化解 = 在 QCoreApplication/QApplication 构造之后调用 `cxSetLocaleNumericC()`(cxvideo_global.h 声明,mpv-examples qt_opengl 官方示例同法)。selftest/demo/qlvotube 已挂,ensure() 内另有兜底。
- **Qt3 信号坑**:connect **基类声明**的信号(如 CXEngine::noticedError)在**派生类实例**上必须用 moc 原样规格 `int,const QString&`;`int,QString` 归一化仅在 Qt6 生效——两套 connect 串按 `const QString&` 书写,Qt3/Qt6 通吃。
- **Qt::QByteArray**:Qt3 是 QMemArray<char>,无 constData(),统一 `.data()`。
- 进度遥测:`mpv_observe_property(DOUBLE)`(time-pos/duration);事件泵线程 0.2s `mpv_wait_event` → CustomEvent 4002 → UI 线程派发(state 事件只映射 FILE_LOADED/END_FILE/IDLE/PLAYBACK_RESTART)。
- selftest 长跑:`cxvideo_selftest --play-probe`(cars 生成 1s 静音 WAV → cmdmpv 进程生命周期回环);环境旋钮 `CX_MPV_AO`/`CX_MPV_VO` 控 mpv 输出后端(headless CI 用 null)。

### 7.2 M2.0 实装记录(2026-11,Qt3/Qt6 双绿;ffmpeg 软解视频链)

- **编译门**:cxvideo.pri `pkg-config --libs libavformat libavcodec libswscale libavutil` 命中才列源/定义 `WITH_CX_ENGINE_FFMPEG`;环境为 FFmpeg 7.1.102(远超 §10 预设的 4.4/6.x,实现全用新 API:send/receive、ch_layout 无涉、pkt_timebase)。
- **文件**(src/):
  - `cxff_wrap.h`:extern "C" 汇入 avformat/avcodec/avutil/swscale(免逐文件 include);
  - `cxpqueue.h`:`CxPkt{pkt,eos}` + `CxPacketQueue`(mutex/cond,cap 满阻塞 push,take 空返回空载体,wake/clear/reset;pkt 所有权交队列,demux push 后置 0 重取);
  - `cxffdec.{h,cpp}`:`CxDecoder`——init(find_decoder+open2+pkt_timebase)/decode(send+receive 循环,余帧 stash deque;**契约:返回帧仅到下一次 decode 有效**,内部 `m_out` 自回收)→convert(sws_getCachedContext 按 (w,h,pix) 缓存,直写 QImage bits;`cxNewImage` 兼 Qt3 无 Format_RGB32)→pts=best_effort×pkt_timebase→ms,duration=frame->duration 或 1/r_frame_rate 兜底;
  - `cxffdemux.h`:`CxDemuxThread`(av_read_frame 循环,按 stream_index 分流 push,EOS 双投两队列,EOF/错误一律按播完处理);
  - `cxffvthread.h`:`CXFFmpegVideoThread` + `CxFFmpegUiEvent`(CustomEvent raw=**4004**,避开 mpv 4002)+节拍等待(pause 120ms 条件等待;M2.0 无音频主时钟,按 1/fps×rate waitPace;renderPts0 之上弃旧帧,首帧越线发 SeekDone)+EOS→drain→Finished;
  - `eng_ffmpeg.{h,cpp}`:`CXEngineFfmpeg` 实装 open(avformat_open_input/find_stream_info/选流/建队列起双线程)/play/pause/stop/seek(av_seek_frame BACKWARD+decoder.flush+queue.reset+vthread.setRenderPts0)/position/duration;customEvent 4004 派发 frame→renderer.showFrame+positionChanged、finished→finished、seekdone→seekFinished。
- **跳坑**:Qt3 `cxengine.h` 只前向声明 CXVideoRenderer → eng_ffmpeg.cpp 需显式 include cxrenderer.h;链接须 `+libavutil`(av_frame_free 符号);`QImage::Format_RGB32` 改 `cxNewImage()`。
  宿主含 vcpkg 目录(坏 libsodium,NEEDED libssp.so.0 缺失)时:avformat→zmq→sodium 的 DT_NEEDED 解析中招 → GNU ld 对 **-L 是后追加者优先**(-L 前置无效),QMAKE_LFLAGS 又先于宿主自己的 rpath-link(会错拉系统 Qt6DBus);正确解法 = **LIBS 内附 `-Wl,-rpath-link,$$FF_LIBDIR`,FF_LIBDIR 由 `pkg-config --variable=libdir libavformat` 推导**,不写死路径(LIBS 恒在命令行 Qt/宿主 rpath-link 之后,缺名才回退轮到它,不误拉);运行时安全:libzmq 是 avformat 传递依赖,libzmq 无 RUNPATH,glibc 走 ld.so.cache → 命中 FF libdir 同源系统库(ldd 实证)。
- **工厂**:cxengine.cpp 补 `CxEngineFfmpeg` 分支(宏门控),舱位 cmdmpv→libmpv→ffmpeg 末位。
- **探针**:`cxvideo_selftest --ff-probe`(ffmpeg CLI 生成 testsrc 2s/160x120/10fps mp4 → ffmpeg 引擎实播,断言 positionChanged 回报数>0);Qt3/Qt6 均绿(frames=20 lastPts=1900 finished=1,)。音频链/音视频同步主时钟与 cmdmpv IPC 仍留 M2.1。

## 8. Qt3 兼容层(逐条)

| Qt5/6 用法 | Qt3 等价 / 化解 |
|---|---|
| QThread::msleep | usleep 包装 `CXThread::sleep(ms)` |
| QThread{run override}/requestInterruption | Qt3.2 有 QThread(run 重写);停止标 CXAtomicInt 自实现(Qt3 无 QAtomicInt) |
| 信号 const 引用 | Qt3 可直接声明;CXVideoFrame 统一以指针出,规避深拷贝 |
| QVariant/QAtomic/property 枚举宏 | 优先用 compat34 已有件,缺则 CXxxx 补 |
| QOpenGLWidget | Qt4 QGLWidget / Qt5.4+ QOpenGLWidget / Qt6 QOpenGLWidget 条件编译,由 cxvideo.pri 依 $${QT_VERSION} 决断 |
| QStringLiteral | `qFromUtf8()`(compat34) |
| QSurface/QWindow 依赖 | 不用;渲染宿主一律 WId |
| QEvent::Kind 自定义 | CustomEvent + 自定 EventType(qlappcom toEventType34 实践) |
| C++ enum class / lambda / chrono | 不用;普通 enum、仿 QtAV 用条件等待/毫秒值 |

## 9. 错误码表(cxvideo 统一)

| 码 | 含义 | 触发 |
|---|---|---|
| CXERR_OK=0 | 成功 | |
| CxErrOpenFail(-10) | 文件/网络流打开失败 | avformat_open_input |
| CxErrNoStream(-11) | 无可播的音视频流 | 流索引查找 |
| CxErrDecode(-12) | 解码持续失败/超阈值 | vthread |
| CxErrAudio(-13) | 音频设备打开失败(静音继续视频) | athread/CXAudioOut |
| CxErrSeek(-14) | seek 目标超界 | demux |
| CxErrEngine(-15) | 引擎底层错误(libmpv event 等) | eng_mpv |
| CxErrWindowLost(-16) | 内嵌宿主窗口被销毁 | renderer host |

统一经 `SIGNAL(error(int,QString))` 上抛;映射 lang 键 `video.err_*`。

## 10. 构建与集成(调用方视角)

```
# qlvotube.pro(qmake,Qt3~Qt6) 片段
include($$PWD/../cxvideo/qtav34/cxvideo.pri)
# cxvideo.pri 内部: INCLUDEPATH + SRC 收集 + 平台库, 以及
# Qt3: 加 CXVIDEO_QT3 define(Qt3 特征限定)
# WITH_CX_ENGINE_MPV / WITH_CX_ENGINE_FFMPEG 条件宏按依赖探测定义(两内核默认恒编)
# Qt6: C++17;Qt3/Qt4: 源码按 C++98 兼容编写
LIBS += -lmpv -lavformat -lavcodec -lswscale -lswresample -lopenal
```
- 依赖版本:FFmpeg ≥4.0(CI 锁 4.4 与 6.x 两条线);libmpv 1.x(≥0.34);OpenALSoft;PulseAudio(条件编译);libass(M3 起,动态加载可免硬依赖)。
- 构建约束:不并行 make,统一走 build*.sh 脚本(沿用本系列规范);CI 四平台(qt3/qt4/qt6 linux、qt5 mac)引用同一 .pri。

## 11. 对接 qlvotube/qskvotube 的落地

- qlvotube:`playerengine.{h,cpp}` 抽象不动,新增 `engine_cxvideo.{h,cpp}` 适配器:CXVideoPlayer↔PlayerEngine 薄映射(open→open+play、position/duration/showFrame 转发、音轨/字幕热切换、错误码→lang 键);探测顺序第 1(见 backend-abstraction.md),`WITH_CXVIDEO` 宏门控,组件可链时才列入候选。
- qskvotube:RendererProbe 的 frameReady(QImage) 直接喂入 QSkinny 侧图像绘制(provider 型接口),落地在 M5。

## 附:实现顺序(对照里程碑)

M0 落 `cxvideo_global.h/cxthread/cxengine.h/cxvideoplayer.h` stub(接口可链、Qt3/Qt6 编译绿)→ M1 eng_mpv → M2 eng_ffmpeg 软解 → M3 全功能 → M4 硬解 → M5 接入 qlvotube[主推]+qskvotube → CI 四路绿。
