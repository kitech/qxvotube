# QtAV 调研评估(2026-01 网搜核实)

为 cxvideo 的立项依据:本节基于 wang-bin/QtAV README/Wiki/Changelog/issues、作者博客(qtav.org/blog/new-sdk)、digiKam 分支维护说明、mdk-sdk README。

## 一、Qt 版本支持矩阵

| Qt 形态 | 原版 QtAV | 说明 |
|---|---|---|
| Qt 3.x | **不支持** | 官方仅声明 Qt4/Qt5("built with both Qt4 and Qt5");即 qlvotube 的 Qt3 构建无法直接使用——这是自研 cxvideo 的首要动机 |
| Qt 4.x Widgets | 支持 | `CONFIG += avwidgets`;QGLWidget 路线 |
| Qt 5.x Widgets | 支持(主战场) | `QT += avwidgets`;1.12+ 有完整 CMake |
| Qt 5.x QML | 支持 | `import QtAV 1.6`(QQuickItemRenderer,OpenGL FBO 直挂场景图) |
| Qt 6.x Widgets | **原版不支持;仅 digiKam fork** | Gilles Caulier(digiKam)移植完成、丢弃 QML、闭门维护(invent.kde.org/graphics/digikam core/libs/video/qtav);原版 cmake 移植不完整,Qt6 构建普遍失败(issue #1389) |
| Qt 6.x QML/Quick | **不可行且作者拒绝** | 作者:"Won't support qt6 qml because qt6 turns to use rhi instead of opengl. Rewritting the whole rendering code is required"。原 QQuickItemRenderer 依赖 OpenGL FBO 与场景图对接,Qt6 换 RHI 后必须整体重写 |

维护状态:最后正式版 **v1.13.0(2019-07)**;README 自述 "I'm not developing QtAV, patches are still welcome";issue 社区共识"项目实质停更"。

## 二、机制/架构

总编排:**AVPlayer**(句柄/状态机/载入/seek/API 面),驱动三线程流水线:

```
媒体源(本地/http/rtsp/qrc 自定义 AVInput)
   ↓ AVDemuxThread: AVDemuxer 逐包读 → 按流分流进包队列
   ├→ VideoThread: AVVideoCodec 解码 → VideoFrame(必要时 swscale 转 VO 期望像素格式)
   │                → AVOutputSet.sendVideoFrame → 各 VideoRenderer
   └→ AudioThread: AVAudioCodec 解码 → AudioFrame → resampler(采样率/声道/速度)
                    → AudioOutput::play(chunk, pts)
```

- **主时钟 = AudioThread**(audio pts 不断 `clock->updateValue`);VideoThread 按 `clock->delay()` 提前 waitAndCheck、迟到丢帧;音比视频晚则视频跳帧同步。
- **seek**:render_pts0 + sync_id 跨线程握手——解码线程一次性跳到目标 pts,全局 audio/video seekFinished。
- **VideoRenderer 统一渲染接口**(运行时切换):QPainterRenderer(软渲 QImage)、GLWidgetRenderer2(QGLWidget/OpenGL ES2,10bit/shader 滤镜)、X11Renderer/XVRenderer(X11 直渲)、Direct2D/ GDI+(win)、QQuickItemRenderer(QML)。支持"1 player 多 renderer"。
- **AudioOutput 后端**:Windows=XAudio2(1.8+ 默认,XP~Win10/Store)、macOS/iOS=AudioToolbox/SystemOpenAL、Linux=OpenAL(推荐)/PulseAudio/null。
- **可扩展工厂**:AVDecoder/AudioOutput/Filter/IVideoRenderer 注册表;硬解 DXVA2、D3D11、VAAPI、VDA/VideoToolbox、CUDA(0-copy)、CedarX、MediaCodec。
- **QML API** 与 QtMultimedia 兼容(可替代 qtmultimedia)。
- 播放速度=重采样速度+时钟 speed;帧步进 playFrameByFrame;视频 EQ(亮度/对比度/饱和度/色度,软+GL 双路)。

## 三、依赖组件

| 组件 | 角色 | 备注 |
|---|---|---|
| Qt Core/Gui | 线程/事件/图像 | QtAV 1.5 起主模块仅依赖 QtCore+QtGui;QtAVWidgets 渲染器/QML 可选 |
| **FFmpeg ≥1.0** / Libav ≥9 | demux/decode/swscale/avfilter | 推荐最新版;1.13 "Use FFmpeg 3.0 API、compatible with new ffmpeg" |
| OpenAL | Linux 音频输出推荐 | Windows 1.8+ 用 XAudio2 不需要 |
| libass | ASS 字幕渲染 | 可动态加载(capi 子模块) |
| libva-dev / libxv-dev / libpulse-dev / libegl1-mesa-dev | Linux 硬解/XV/脉冲 | Ubuntu apt 命名 |
| QtAV-depends-windows | win 懒人包 | sourceforge 发布(FFmpeg/OpenAL 头+库) |
| 构建 | qmake(QtAV.pro)/CMake(1.12+ QTDIR) | shadow build 推荐 |

许可:LGPL v2.1+(库)/GPL v3(播放器示例)→ 以 LGPL 动态链接给应用可商用。

## 四、继任项目 mdk-sdk(同样来自 wang-bin)

- 纯 C++17 无 Qt 依赖、二进制闭源 SDK;C API 稳定(libmdk),Qt 集成才用示例(QMDKWindow=QOpenGLWindow / QMDKWidgetRenderer=QOpenGLWidget / qmlrhi=RHI 路线(支持 Vulkan/OpenGL/D3D11/Metal,Qt≥5.14/Qt6) / qtpaintonvideo=QPainter 直绘)。
- 渲染引擎 OpenGL/D3D11/12/Vulkan/Metal,0-copy 各平台;智能 FFmpeg 运行时(4.0~9.x 二进制兼容动态加载)。
- **闭源约束**:免费但需向作者取 key;无 key 版"最后一帧出二维码"且要求约每月更新;不接受 GPL 全源开源(作者 2025-05 issue #300 明确拒绝)。qtmultimedia 插件(Qt5)也基于它。

## 五、对 qlvotube/qskvotube 场景的结论

| 场景 | 可用方案(按推荐序) |
|---|---|
| qlvotube Qt3 构建 | **只有"进程 mpv / 自研"能走**——QtAV 死、mdk 只是 C-API(集成也需自写 Qt3 壳) |
| qlvotube Qt4 构建 | QtAV(能编但无维护,FFmpeg ABI 漂移风险)不列主线;同 Qt3 走 mpv/cxvideo |
| qlvotube Qt6 Widgets 构建 | ProcessMpv → QMediaPlayer → digiKam fork QtAV(风险:闭门无 QML)/ mdk-sdk(闭源约束) |
| qskvotube Qt6 Quick/RHI | 场景内嵌现实**仅 mdk-sdk qmlrhi**;开源路线:qskvotube 短期浮窗 mpv |
| 全版本统一 | **cxvideo(我们自研)**:API 一套、eng_mpv+eng_ffmpeg 双内核,见 cxvideo-plan.md/cxvideo-detail.md |

结论口诀:QtAV 设计价值(线程模型/渲染器抽象/时钟与同步)照搬,cxvideo 落地;原 QtAV 与 mdk-sdk 都作为备选第三引擎不入主线(CI 不配)。

## 附:参考链接

- 主仓: https://github.com/wang-bin/QtAV
- Use in projects wiki: https://github.com/wang-bin/QtAV/wiki/Use-QtAV-In-Your-Projects
- Build wiki: https://github.com/wang-bin/QtAV/wiki/Build-QtAV
- Qt6 讨论: https://github.com/wang-bin/QtAV/issues/1389
- 作者博客(new-sdk): https://www.qtav.org/blog/new-sdk.html
- digiKam QtAV 维护: https://invent.kde.org/graphics/digikam/-/tree/master/core/libs/video/qtav
- mdk-sdk: https://github.com/wang-bin/mdk-sdk
- mdk Qt 示例(QMDKRenderer/qmlrhi): https://github.com/wang-bin/mdk-examples/tree/master/Qt
- GPLv3 开源请求被拒(闭源观点): https://github.com/wang-bin/mdk-sdk/issues/300
