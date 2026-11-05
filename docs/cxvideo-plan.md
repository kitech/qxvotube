# cxvideo 总规划

前置阅读:`qtav-evaluation.md`(QtAV 调研与结论)、`backend-abstraction.md`(qlvotube 侧抽象)。

## 定位

**qxvotube/cxvideo** —— 本仓库 `cxvideo/` 内的播放组件区,与源码同仓管理;供 qlvotube/qskvotube 复用。自研主目录 `cxvideo/qtav34`,另有两份 vendored 参照快照(见 `cxvideo/README.md`):

- `cxvideo/qtav56/` —— wang-bin/QtAV 原版 master 快照(`18499f72`,2025-08-16),已删其 `qml/` 模块(widgets-only)
- `cxvideo/qtavqml/` —— nejatafshar/QtAV fork 快照(`8af333e9`,2023-02),统一静态库含 QML,Qt5
- 机制基准以 qtav56 为准(fork 与原版互不包含,86 文件级差异);qtavqml 仅 QML 参考

- **一句话**:QtAV 的机制(三线程、渲染器抽象、时钟同步)+ mdk 的封装思路(libmpv 内核可选)+ **Qt3 首等公民**(这恰是两者都做不到的)→ 我们自己的跨 Qt 播放框架。
- **API 兼容承诺**:同一组头文件、同一公共 API 在 **Qt3/Qt4/Qt5(可选)/Qt6** 全版本编译通过,业务码零 #ifdef(排除 Qt3 特征不足之处与 compat34 宏,见 cxvideo-detail.md)。
- 命名前缀 `CXVideo*` + 全局命名空间(暂无 namespace,参照 qlcomp 风格)。

## 已锁定决策

| 决策点 | 结论 |
|---|---|
| 放置 | 本仓库 `cxvideo/qtav34/`(自研主线);`cxvideo/qtav56/` 与 `cxvideo/qtavqml/` 为 vendored 参照快照(详 `cxvideo/README.md`) |
| 公共 API 风格 | QtAV 风格照搬改名:CXVideoPlayer / CXVideoRenderer / setRenderer / play / state 信号(详表见 cxvideo-detail.md) |
| 解码内核 | **两个都要**:`CXEngineMpv`(libmpv C-API 内嵌)+ `CXEngineFfmpeg`(全自研 FFmpeg 线程管线);运行时枚举选择,失败自动降级 |
| 功能目标 | 完整功能(对齐 QtAV 功能面):轨道选择/字幕/倍速/EQ/截图/硬解;分里程碑交付 |
| Quick/qskvotube | 渲染器 QImage 帧回调(probe)模式,Quick 场景不嵌原生窗口(M5) |

## 双内核架构

```
                    ┌ CXVideoPlayer(编排/状态/信号)
                    │     └ CXEngine(IEngine 接口)
                    │           ├ CXEngineMpv    —— libmpv(mpv_render_context / wid 内嵌)
                    │           └ CXEngineFfmpeg —— 自研线程管线(复刻 QtAV)
                    │                 ├ CXDemuxThread ─→ CXVideoThread / CXAudioThread
                    │                 ├ CXClock(audio 主时钟/同步/seek 握手)
                    │                 ├ CXDecoderFactory(avcodec/硬件 vaapi dxva2 探测)
                    │                 └ CXAudioOut(OpenAL/PulseAudio C-API,不依赖 Qt)
                    └ CXVideoRenderer 接口
                          ├ CXRendererSoftware(QImage→widget paintEvent,Qt3 兼容首选)
                          ├ CXRendererX11(X11 直渲 winId,Qt3/6 均可,无 GL 依赖)
                          ├ CXRendererGL(QGLWidget→QOpenGLWidget 双 Qt 实现由条件编译选择)
                          └ CXRendererProbe(无窗口 QImage 帧回调,给 Quick/测试)
```

- 线程:Qt3.2+ 有 QThread;统一包 `CXThread` 壳(底层原生 QThread),cxvideo 内部不直接 include 线程平台件之外的东西,可移植点集中在壳内。
- 音频输出绕过 Qt(Qt3 的 QSoundBuffer 太弱):OpenAL(全平台可用)优先,PulseAudio 备选,win 上 OpenALSoft 即可,不引 XAudio2(简化,mdk/QtAV 差异之一)。

## 里程碑

| M | 交付 | 验收 |
|---|---|---|
| M0 | 仓库骨架+接口头+CXEngine 探测工厂+demo(独立 tools/demo) | 无媒体也能编过 Qt3/Qt6;接口可链 |
| M1 | CXEngineMpv 走通:open/play/pause/stop/seek/volume/时长/进度信号;Qt3 X11 嵌入;qlvotube PlayerEngine 挂接 | qlvotube(q3/q4/qt6)播 mp4/m3u8 成功 |
| M2 | CXEngineFfmpeg 软解(mp4/H264+AAC、mp4/fMP4、http/https、HLS m3u8);CXRendererSoftware/X11;音画同步基准 | Qt3 本地/网络 mp4 播完不漂移>1s;selftest 时序测试 |
| M3 | 完整功能:倍速/字幕(嵌入+外挂 srt/ass 经 libass 动态加载)/音轨字幕轨切换/截图/视频 EQ | demo UI 全操作点过;Qt3 全绿 |
| M4 | 硬解:VAAPI(linux)/DXVA2(win)探测与回退;DASH/分轨音频(合流段) | 4K60 软/硬解 CPU 对比可用 |
| M5 | 接入面收口:qlvotube PlayerEngine 主推 cxvideo;qskvotube RendererProbe 帧回调;CI(不并行,无 -j)四 Qt 配置 | CI 四路全绿 |

## 构建

- `cxvideo.pri`(qmake,qlcomp 模式):调用方 `include($$PWD/../cxvideo/qtav34/cxvideo.pri)`(M0 起新仓布局);Qt3 定义由 pri/调用方注入 `QT3_BUILD`(与 qlvotube 同款 isEmpty(QT_VERSION) 自判)。
- `CMakeLists.txt`(可选,Qt5.15+/Qt6,布局与 qskcomp.cmake 对齐)。
- 依赖:libmpv(M1 内核,系统包/自编译);FFmpeg(libavformat/avcodec/swscale/swresample ≥4.0,M2 内核);OpenALSoft/PulseAudio(M2);libass(可选,M3);libva/dxva(M4,条件编译)。
- 代码不引用视频相关 Qt 模块(不用 QtMultimedia);Qt6 私有头只在 CXRendererGL 条件编译内使用,保持主模块干净。

## 风险

| 风险 | 缓解 |
|---|---|
| Qt3 无 OpenGL 常规件 | 主渲染路径 Software/X11;GL 仅作为 Qt4/6 加成 |
| FFmpeg 新版 ABI 漂移 | 锁 CI 版本(4.4/6.x 两条线跑);源码内不触碰 deprecated API |
| libmpv 平台打包 | sys 包先走,win/mac 用 mdk 思路的 portable 提示;文档明确路径探测顺序 |
| 音频设备绕开 Qt | OpenALSoft 通吃;无声环境用 null 输出测试 |
| anystik 是另一个仓库 | 骨架任务单独立项,本次仅 docs |
