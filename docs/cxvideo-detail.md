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
- **探针**:`cxvideo_selftest --ff-probe`(ffmpeg CLI 生成 testsrc 2s/160x120/10fps mp4 → ffmpeg 引擎实播,断言 positionChanged 回报数>0);Qt3/Qt6 均绿(frames=20 lastPts=1900 finished=1,)。音频链/主时钟见 §7.3。

### 7.3 M2.1 实装记录(2026-11;音频链/主时钟,Qt3/Qt6 双绿)

- **网搜核对**:FFmpeg 7.1 doxygen(`swr_alloc_set_opts2`+AVChannelLayout、`swr_get_out_samples` 上界、`swr_convert(NULL,0)` 冲洗);freedesktop pulse-simple(阻塞 write、get_latency µs、drain/flush,无 pause 原语);alsa PCM R/W(`set_params` 500ms、writei 短写循环、-EPIPE→`snd_pcm_recover`、drain 后必须 prepare);SDL2 wiki push 模式(callback=NULL、`SDL_QueueAudio` 拷贝、`GetQueuedAudioSize`);AAC priming `skip_samples` 由 libavcodec decode 自动丢弃,残余负 pts 在 athread 钳 0。
- **新增文件**(src/):
  - `cxffclock.h`:`CxClock`(QMutex+QWaitCondition;value/updateValue/setSpeed/waitAndCheck(delay/speed)/interrupt/reset;header-only);
  - `cxffdeca.{h,cpp}`:`CxDecoderAudio`——decode 同 CxDecoder m_out/stash 契约;convert(in,outCh,outRate) 用 swr→交错 S16 写内部 av_malloc arena(按 `swr_get_out_samples` 上界,增长复用),convertFlush 冲洗;flush 重建 swr;按 **传入帧** 的 ch_layout/format/sample_rate 建 swr;
  - `cffao.{h,cpp}`:`CXAudioOut` 抽象(open/write 阻塞/pause/resume/clear/latencyMs/drain/close + outCh/outRate);silent 恒编(实时节流虚拟延迟,`gettimeofday` 时钟);工厂 `cffaoCreate(want)`:**auto=alsa→pulse→sdl→silent**(Linux ALSA 直通最稳;pulse-simple 阻塞写不可中断,列为次级/显式;SDL 跨平台兜底;CI 无设备落 silent);
  - `cffao_pulse.cpp`/`cffao_alsa.cpp`/`cffao_sdl.cpp`:宏门控 `WITH_CX_FF_AO_PULSE/ALSA/SDL`;SDL push 水位 ≈120ms 轮询(带等待上限,防 device 暂停卡死);
  - `cxffathread.{h,cpp}`:`CXFFmpegAudioThread`——take→decode(负 pts 钳 0)→convert→≈40ms 分块 writeScaled(软音量/静音 int16 缩放)→`clock.updateValue`+post `CxFFmpegEvAudioPos`;倍速按块丢弃/复写补偿(m_skipAcc/m_dupAcc);EOS→convertFlush 残余→`ao->drain()`→Finished。
- **修改**:
  - `cxffvthread.h`:emitFrame 主时钟分支——每帧重算 `target=pts−ao->latencyMs()`,未到则 ≤100ms 打盹由 clock 唤醒重算;无音频回退 M2.0 盲等;新增 setClock/setAo;事件枚举增 `CxFFmpegEvAudioPos=6`;
  - `eng_ffmpeg.{h,cpp}`:m_adec/m_athread/m_ao/m_clock/m_seekPend/m_eosPend;open 建音频链(adec init 失败→aIdx=-1 降级;AO open 失败→silent 兜底并发非致命 CxErrAudio);seek 用 `sIdx=vIdx≥0?vIdx:aIdx`(纯音频可 seek),flush 双解码器+双队列 reset+ao clear+clock reset,双线程 setRenderPts0;setRate/setVolume/setMute 接入 athread;`aoBackend()` 访问器;finished 由 m_eosPend 计数(视频+音频皆完),SeekDone 由 m_seekPend 防重;AudioPos 仅在纯音频时驱动 positionChanged;stopThreads 顺序 vthread→athread→demux→queues→dec/adec→ao→clock→fmt,**athread wait 超时(不可中断后端)则泄漏该组对象而非 delete 运行中线程**;
  - `cxvideo.pri`:FF_DEPS/FF_CFLAGS 增 **libswresample**;AO 后端 pkg-config 探测(命中才编入+宏);新增 7 源/5 头;
  - `cxff_wrap.h`:补 swresample/avutil opt/channel_layout;
  - `tools/selftest/main.cpp`:`--ff-probe` 媒体升级 testsrc+sine `-shortest` → 默认 H.264(libx264)+AAC;打印 `ao=` 与 frames;frames<10 或音频链缺席仅 WARN(CI silent 不假红)。
- **实测**(Qt3/Qt6,双绿):基线两侧 `0 failures`;`--ff-probe` 默认 silent 与显式 `CX_FF_AO=sdl` 均 `OK (ao=… frames=20 lastPts=1900 finished=1)`;`--play-probe` OK;`qlvotube` Qt3/Qt6 重链绿(ldd:libswresample/alsa/SDL2 NEEDED 正常,sodium 走系统库,无 Qt6DBus 错配)。
- **环境限制(实测记录)**:本机仅有 HDMI 卡(无活跃 sink,pipewire 兜底)——alsa(`default` 路由 pipewire)与 pulse **open 成功但写端得不到消费**,`writei` 阻塞不可中断 → athread 卡写,EOS 的 `finished` 拿不到(Qt3/Qt6 症状一致;此前 Qt3 记录 "alsa finished=1" 系当时 pipewire 尚有消费端的环境时点结果)。故探针在未设 `CX_FF_AO` 时默认 `silent`(EOS 全链路可测);有真音频设备的机器用 `CX_FF_AO=auto` 验 alsa/pulse/sdl。引擎侧 auto 顺序仍为 alsa→pulse→sdl→silent(真机优先直通);无 sink 环境 auto 落 alsa 即会阻塞,此为其已知形态。

### 7.4 M3-A(M3.1)实装记录(2026-11;轨道+双内核字幕,Qt3/Qt6 双绿)

- **API 契约锁定**(cxengine.h / cxvideoplayer passthrough):`audioTrackCount()/subtitleTrackCount() const`、`setAudioTrack(int)/setSubtitleTrack(int)`(-1=关)、`setSubtitleFile(const QString&)`(SRT);信号 `subtitleChanged(const QString&)`(空串=清屏;Qt3 connect 用 `const QString&` 字面规格);M3-B 占位 `setEq(const CXVideoEq&)`(CXVideoEq 落 cxvideo_global.h,-100..100)。
- **ffmpeg 端(非事件流设计)**:第三解包队列 sq(容量 128)+ `CxFFSubDecoder`(avcodec decode_subtitle2;FFmpeg 7.1 仍可用仅 deprecated,#pragma 压制警告)+ header-only `CXFFmpegSubThread`;cue 落 `CxFFSubTimeline`(手滚数组+QMutex);UI 线程 dispatchEvent 于 Frame/AudioPos 处 `cxScanSub(ms)` 查轴、变化即发——**无新事件类型**。内嵌时间轴 `m_subTl` open 清零;外挂 `m_subTlExt` 独立保留(重启不清);外挂优先于内嵌。
- **实测踩坑(关键)**:mov_text 解码出的 AVSubtitle `end_display_time=0`(时长只在 packet duration)——按 `end<=start→pkt duration(m_tb 换算,兜 3s)` 兜底,否则 cue 起止相同永不出字;另 mov_text 译出为 ASS Dialogue 形(如 `0,0,Default,,0,0,0,,正文`,字段数 8,标准 ASS 9),固定按 9 个逗号跳不过——改为 **按字段内容跳**(空/纯数字·时戳/Default 样式视为前缀字段,首个正文段起取),再走 `cffSubStripTags`(ASS 花括号/HTML 标签/`\N`→`\n`)。
- **ffmpeg 换轨 = 引擎重启式**:`reopenAt(posMs, wasPlaying)`——stopThreads(保留 m_url/m_subFile/m_subTlExt/m_rateCur 等)→open(prompt 重选)→重放 rate/vol/mute→seek(pos)→非播放则 Pause;轨道序以 **枚举序**(prompt n=0 起第 n 个;-1=默认首个;-2=关);幂等(与当前同轨)返回 false 不重启。`m_eosPend` 三路计数(视频+音频+字幕线程都完才 finished;Finished 时清屏发 subtitleChanged(""))。
- **内嵌只收文本型**:mov_text/subrip/ass/ssa/webvtt/TEXT(`cffSubCodecTextual`);bitmap 型(pgs/dvb/dvdsub)跳过。
- **mpv 端**:track-list 用 `mpv_get_property`(`track-list/count|i/type|i/id` 逐项枚举,**不引 mpv_node 镜像**;符号可选,缺则 0/false);选轨 `set aid/sid <id|no>`;外挂 `sub-add <path>`(默认 select);字幕文本 =泵 observe `sub-text`(STRING)→ strdup → `CxMpvUiEvSubtitle=8` 泼 `subtitleChanged`。**cmdmpv 无 IPC 不承诺**(基类默认 0/false 已兜,eng_mpv_cmd 不改)。
- **selftest**:`--ff-probe` 媒体升级为 testsrc+sine+**外挂 SRT(-c:s mov_text 内嵌)** 同一份 mp4;新增 **`--sub-probe`**(断言 subtitleChanged 非空 ≥1);踩坑:三输入 `-map 0:v -map 1:a -map 2:s`(各输入分开计数,`0:a` 对 srt 输入文件无效)。`ffprobe_counter.h` 增 onSubtitle/subN/lastSub。
- **实测**(Qt3/Qt6,双绿):基线两侧 `0 failures`;`--ff-probe OK (ao=silent frames=16 lastPts=1800 finished=1)`;**`--sub-probe OK (events=2 streams=1 first=CXVIDEO-PROBE2)`**;`--play-probe` OK;`qlvotube` Qt3(q3votube)/Qt6(q6votube)重链绿、ldd 无缺。

### 7.5 M3-B 实装记录(2026-11;EQ 软路 LUT/倍速 splice crossfade/截图/demo,Qt3/Qt6 双绿)

- **EQ 软路 LUT**(renderer_software):`CXVideoPlayer::setEq → CXEngineFfmpeg::setEq → p_renderer->setVideoEq`;brightness(±64/255 偏移)+ contrast(绕 128 乘强,3‰/点)合成**单张 256 项 LUT**(RGB 三通道共表),saturation 按 luma(299/587/114)加权混合(permil,0..2000);`m_eqOn` 中性旁路零开销;生效在 paint 期 shallow 拷贝上(m_img 原始帧不动),EQ 变更 `update()` 立即重绘。**hue 不做软路(M4 GL);mpv/cmdmpv 不承诺**。renderer_probe 侧 EQ 仅 no-op 声明(setVideoEq 默认虚 + probe 未覆写)。
- **倍速 splice crossfade**(cxffathread):skip 块边界 128 帧(≈2.9ms @44.1k)线性斜坡——`skipNext` 用 `m_skipAcc + (rate-1)*chunkSamples >= chunkSamples` 前瞻(与后续判弃同式),命中则本块尾渐出;`prevSkip` 记上一块被弃,命中则本块头渐入;dup 路径为同源复写(无拼接不连续)不作处理。音量/静音仍走 writeScaled(crossfade 先于缩放,互不干扰)。
- **截图**:接口 `CXVideoRenderer::saveScreenshot(path)` 默认 false;软渲保存**当前帧 LUT 后** PNG(`QImage::save`,Qt3 亦此 API);`CXRendererProbe` headless 覆写保存最近原始帧(无 QWidget 可用于无 GUI app 的探针);_passthrough_ `CXVideoPlayer::saveScreenshot → CXEngine::screenshot → renderer`。
- **探针新增**:`--shot-probe`(setEq b30/c20/s40 → 播放中 singleShot 600ms 截图 → QFile size>1000 断言;Qt3/Qt6 双 OK,size≈7.7KB)、`--rate-probe`(setRate(2.0) 全片,断言 frames>0 且 finished=1)。`CxShotWorker`(QTimer singleShot 截图转子)落 ffprobe_counter.h(须头文件供 moc)。
- **demo**(cxvideo_demo):带文件参数即 `player.open+play`(`finished→app::quit`),M0 的 64x64 蓝帧自证仅无参时注入;无参行为不变。
- **实测**(Qt3/Qt6,双绿):基线 `0 failures` 不回退;五探针 `--ff-probe/--sub-probe/--shot-probe/--rate-probe/--play-probe` 全 OK;`qlvotube` q3votube/q6votube 重链绿。

### 7.6 M4-A 实装记录(2026-11;ffmpeg 硬解探活 + GL 渲染器/--hw-probe/--gl-probe,Qt3/Qt6 双绿)

- **hw 探活序**(cffhw):`cffHwProbe` 按 **vaapi → cuda** 序逐个 `av_hwdevice_ctx_create` 试建(建即释),命中返回类型;`ffHw` 均不命中时全程软解,现有路径零改动。`CxDecoder::open` 内 `initHw()`:命中则挂 `hw_device_ctx`(av_buffer_ref 第二引用 + 探活 ref 立即 unref——**直接赋值=1 ref 双 owner→close 期 double free,已在 glibc 崩实修**)+ `get_format` 回调。
- **get_format 回调**(cxffdec):全局 `cxCurHwPix` 槽记录要挑的 hw pixfmt(VAAPI/CUDA);命中 hw 名单内则选之,**未命中/进入失败时取首个非 HWACCEL 软格式**——decoder 内部挖起(h264 "Failed setup for format cuda")不毒管线。
- **hw 帧下载**:receive 出帧格式带 HWACCEL flag 即 `av_hwframe_transfer_data` 落系统内存(NV12)进现有 sws 管线(**零拷贝 surface 二期**);下载失败当场弃本帧。播放中途落软路(get_format 未命中后 decoder 回软)则**熄火 hw 名**:`m_hwName→0`、收 our 引用,`hwName()` 如实报 0(本机 vaapi 连接失败/cuda 无 SDK 即此形态,probe 判 SKIP 而非 OK)。
- **GL 渲染器**(renderer_gl,\*\*无 Q_OBJECT\*\*——moc 不解析条件基类段):
  - Qt3/Qt4:QGLWidget 固定管线,RGB32 纹理 quad(`swapRGB` 转 RGBA 上传,不赌 GL_BGRA 扩展);EQ 走 **CPU LUT**(cxEq.h `cxEqApplyRgb` 预处理后上传;固定管线不赌 glColorMatrix);截图用最近 CPU 帧保存。
  - Qt5.4+/Qt6:QOpenGLWidget + **core profile 必须走 VAO/VBO**——首版 `setAttributeArray` 用裸指针数组(驱动把 guest 内存当 attrib 源)在 core 下 launch libgallium 段错误;**修法 = QOpenGLVertexArrayObject + QOpenGLBuffer 交错 VBO(aPos|aUV,setAttributeBuffer),letterbox 改 shader `uScale` uniform**,探针即绿。fragment shader uniforms:brightness/contrast/saturation + **hue mat3**(YCbCr 色度平面旋转近似,QMatrix3x3 float 构造);截图 `grabFramebuffer` 自带回读。
- **共用 EQ 工具**(cxeq.h):LUT 构建/中性判断/saturation permil/RGB32 应用四件抽出,renderer_software(M3-B 原实现搬移,行为零变)与 renderer_gl(Qt3 侧)双吃。
- **探针新增**:`--hw-probe`(有 hw→OK 打名,无 hw→SKIP(0 exit,软解流 finished=1 断言照旧)——INIT 均如实反映形态)、`--gl-probe`(GUI-enabled app 实渲一程;main 先于 app 构造预扫 argv——Qt6 恒 QApplication、Qt3 `QApplication(argc,argv,glProbe)`)。**gl-probe 前置 --ff-probe 产媒体**。
- **pri**:`cffhw/renderer_gl/cxeq` 三件 + `LIBS += -lGL`(两 Qt 均需)+ `greaterThan(QT_MAJOR_VERSION,5): QT += openglwidgets`;eng_ffmpeg.h 补 include cxffdec.h 完整型(inline hwName)。
- **实测**(Qt3/Qt6,双绿):编 0 failures;基线 0 failures 不回退;七探针(旧五 + hw/gl)Qt3 全 OK/SKIP、Qt6 全 OK/SKIP;`qlvotube` q3votube/q6votube 重链绿(ldd GL 库齐,无 not found)。

### 7.7 M4-B 实装记录(2026-11;mpv render_context 渲染路 + EQ/截图补齐,Qt3/Qt6 双绿)

- **route 选择与回退**(eng_mpv):`open()` 先试 **gl 路**(`p_host` 有 + render 四件套符号齐:`mpv_render_context_create/free/render` + `set_update_callback`),`vo=libmpv`、`force-window=no`、自建 `CXRendererMpvGl` 挂 host;`render_context_create` 失败或 `mpv_initialize` 失败 → `closeInternal()` 重建走 **wid 路**(M1/M2/M3 原路径零改)。`renderPath()` 如实报 `"mpv-gl"`/`"mpv-wid"`/`0`。**注意 render_context_create 是 3 参**(`(ctx**, mpv_handle*, params)`),镜像 typedef 首版漏 handle 编译即炸。
- **update 驱动**(renderer_mpvgl):mpv 内部线程回调 `cxMpvGlUpdateHook` → `QApplication::postEvent(view, QEvent(User+4710))` → GUI 线程 `event()` 里 `update()`。**坑:Qt3 `QApplication::sendPostedEvents` 派发后自己 `delete` 事件,我们再 `delete ev` 即 double free 崩**(首版即踩,段错误在 sendPostedEvents)。故 `event()` 只 return true 不 delete。
- **paintGL**:`mpv_render_context_render(OPENGL_FBO + FLIP_Y)` 直渲当前 FBO(0),成功后 `report_swap`;不产自家几何/不碰自家 GL 状态。Qt6 core profile 仍须**空 VAO**(`QOpenGLVertexArrayObject::create()+bind()`,core context 无 VAO 时 attrib 相关默认态非法);Qt3 QGLWidget 无需。
- **get_proc_address**:Qt3 用 `glXGetProcAddressARB`(QGLContext 无该 API),Qt6 用 `QOpenGLContext::currentContext()->getProcAddress`;**须在 makeCurrent 后 create**(engine 侧显式 makeCurrent/doneCurrent)。
- **EQ 直通**(eng_mpv::setEq):`brightness/contrast/saturation`(-100..100 与 `CXVideoEq` 同域)、`hue`(±180)直接 `mpv_set_property(INT64)`;**mpv vo 未初始化时属性不可用(-9),此时返回 false 如实**(探针记录 eq=0/1 均可容忍,但 gl 路正常播放后实测 eq=1)。
- **截图**(eng_mpv::screenshot):`screenshot-to-file` 命令,仅 `mpv-gl` 路;wid 路恒 false。**坑**:`keep-open=no` 下 EOF 后无帧可截,必须**播放中**调用(探针 600ms singleShot;`CxShotWorker` 加 path 参数复用)。mpv 软截图回退(`Falling back to software screenshot`)可正常落 PNG。
- **发现的 M1 潜伏缺陷(已修)**:**`CxMpvFormat` 枚举整套错位**——旧镜像把 `OsdString/Flag/Int64/Double` 排成 5/6/7/8,官方冻结序是 **OSD=2, Flag=3, Int64=4, Double=5**。后果:`time-pos/duration` 以 Double=8(=NODE_MAP)观测**永不投递** → mpv 内核 `positionChanged` 从未走过;`track-list/count` Int64 同错。M3-A 字幕侥幸用 String=1 正确值才通。修复后 mpv-gl 探针 frames=15 正常。**教训:dlopen 镜像的枚举冻结值必须逐条对官方 client.h**。
- **探针新增**:`--mpv-gl-probe`(GUI 前提;main 预扫与 `--gl-probe` 同;断言 `renderPath=mpv-gl` + frames>0 + finished=1 + EQ 设置 + 播放中截图 PNG>1KB;符号缺/无 GL → SKIP 或 wid 回退帧流断言)。`CX_MPV_DEBUG=1` 打开 mpv terminal 日志与 EQ/pos 调试印。
- **pri**:`renderer_mpvgl.{h,cpp}` 入 SOURCES/HEADERS。
- **实测**(Qt3/Qt6,双绿):编 0 failures;基线 0 failures;八探针(旧七 + mpv-gl)全 OK/SKIP(`mpv-gl-probe: OK (render=mpv-gl frames=15 eq=1 shot=1)`);`qlvotube` q3votube/q6votube 重链绿。

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
