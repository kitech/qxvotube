# cxvideo

本目录是 qxvotube 的播放组件区,三子目录各司其职。机制调研依据见
`../docs/qtav-evaluation.md`,自研设计契约见 `../docs/cxvideo-detail.md`。

## 目录与版本

| 目录 | 内容 | 上游快照(commit sha) | Qt 支持 | 角色 |
|---|---|---|---|---|
| `qtav56/` | wang-bin/QtAV 原版源码快照,**已删除其 `qml/` 模块**(QtAV.pro/CMakeLists.txt 的 BUILD_QML 块已同步注释掉) | `18499f7259ee6eabd24eef2e264dad0f2d32932b` 2025-08-16(原 master) | Qt4 + Qt5 官方;Qt6 无官方支持 | 机制参考(线程组/时钟/渲染器抽象)+ Qt5/Q4 可直接用 |
| `qtavqml/` | nejatafshar/QtAV fork 快照(统一静态库含 QML 模块、录屏/推流、低延迟修复) | `8af333e997429f14fbfbf1cd484f27c48ea5a304` 2023-02-02 | Qt5(qmake) | QML 路线参考(供 qskvotube Quick 内嵌评估) |
| `qtav34/` | **全新自研**(无上游):CXVideo* API、Qt3(C++98) 首等公民 | ours,自 v0.1.0 M0 起步 | Qt3(/opt/qt338sh)+ Qt4 + Qt6(C++17) | 主线:qlvotube PlayerEngine 的 Primary 引擎 |

## 血缘说明(重要)

qtavqml 与 qtav56 是同一祖先的**两个独立快照,互不包含**:

- fork 基于上游 ≈2022 基线 + fork 自有补丁;上游 master 在 fork 点之后又新增
  VulkanRenderer、src/shaders(SPV)、depends 脚本等。
- 内容级差异:`src/`+`widgets/` 共 86 个文件内容不同(含
  AVPlayer.cpp/AVDemuxer.cpp/AVDemuxThread.cpp/VideoThread.cpp/AudioThread.cpp
  这些内核文件——fork 修过 load/stop 阻塞主线程等问题);examples 差 201 处。
- 因此:**qtav34 自研的机制基准以 qtav56(原版 master,最新)为准**;
  qtavqml 仅作 QML 渲染参考,其 src 改动不向 qtav34 整体回灌
  (其"load/stop 不阻塞"修复等点为选择性借鉴)。

## 许可

- qtav56 / qtavqml:库 LGPL v2.1+(原版示例 GPL v3)——快照内 LICENSE/gpl/lgpl 文件原样保留。
- qtav34:随 qxvotube 主仓约定。

## qtav34 使用方式(qmake,Qt3~Qt6)

```
qlvotube.pro 内:
include($$PWD/../cxvideo/qtav34/cxvideo.pri)
```
- 内核实况(M2.1 后):
  - `WITH_CX_ENGINE_MPV` 恒开,libmpv 走 **dlopen**(不链 -lmpv;缺包 isAvailable() false 自动降级);
  - `WITH_CX_ENGINE_FFMPEG` **已启用**(pkg-config 探到 libavformat/libavcodec/libswscale/libswresample/libavutil 即编入;当前 FFmpeg 7.1.102 实测绿);
  - 音频输出 `WITH_CX_FF_AO_*`:pulse/alsa/sdl 依赖命中才编入,silent 恒编;`CX_FF_AO` 环境变量选(auto=alsa→pulse→sdl→silent);
  - **cmdmpv 内核(runtime PATH 探测 `command -v mpv`)** 恒编;
  - **探测顺序:cmdmpv → libmpv → ffmpeg**(用户约定 cmdmpv 优先)。
- 复用 `qlcomp/compat34.h`(`INCLUDEPATH += ../../qlcomp`,由 pri 自动注入);
  宿主工程已有 compat34.cpp(qlite.pri)时 pri 自动跳过 compatcore34.cpp 重编。
- 构建:`./buildqt3.sh`(Qt3=/opt/qt338sh)/ `./buildqt6.sh`(Qt6=/opt/qt/6.7.3);
  顺序编译、不并行(约定同 qlvotube);产物 `build-qt3*/q3cxv` 等及
  `selftest`(无 GUI 自测,当前 `0 failures`)。
- **locale**:凡接 CXVideoPlayer 的程序入口须在 QApp 构造后调用
  `cxSetLocaleNumericC()`(libmpv 硬性要求 LC_NUMERIC=C,见 detail.md §7.1)。
- 实播自测:`cxvideo_selftest --play-probe` 用 cmdmpv 进程生命周期回环验证;
  `cxvideo_selftest --ff-probe` 用 ffmpeg 内核实播 testsrc+sine mp4(H.264+AAC;视频链+音频链/主时钟)验证,输出 `ao=<backend>` 与帧数;未设 `CX_FF_AO` 时探针默认 **silent**(无 sink/CI 环境下 alsa/pulse 写端阻塞会拿不到 finished;真音频设备可用 `CX_FF_AO=auto` 验 alsa/pulse/sdl)。
  `cxvideo_selftest --sub-probe`(M3-A)验证 mov_text 内嵌字幕的 `subtitleChanged` 事件流(媒体即 --ff-probe 同一份,含内嵌字幕流)。
- 轨道/字幕 API(M3-A):`audioTrackCount()/subtitleTrackCount()/setAudioTrack(-1=关)/setSubtitleTrack(-1=关)/setSubtitleFile(外挂 SRT)` + 信号 `subtitleChanged`;ffmpeg 内核换轨=引擎重启式(缀位/速率/音量保留);mpv 内核 `set aid/sid/sub-add`;**cmdmpv 不支持(无 IPC)**。

## 里程碑对照(docs/cxvideo-plan.md)

M0 ✅(接口可链)→ **M1 ✅(eng_mpv libmpv 内嵌 + eng_mpv_cmd cmdmpv 子进程;Qt3/Qt6 双绿,实播探针过)** → **M2.0 ✅(eng_ffmpeg 软解视频链:demux/vthread 双线程→sws→QImage 事件回投)** → **M2.1 ✅(音频链/主时钟:athread+swr→S16、pulse/alsa/sdl/silent 四 AO、CxClock 每帧校准;Qt3/Qt6 双绿,H.264+AAC 探针)** →
**M3-A ✅(轨道+双内核字幕:audio/subtitle track 选择、外挂 SRT、subtitleChanged 事件;ffmpeg 内嵌 mov_text + mpv track-list/aid·sid/sub-add;Qt3/Qt6 双绿,--sub-probe 过)** →
M3-B(EQ/倍速精修/截图)→ M4 硬解 → M5 接入 qlvotube+qskvotube。
(注:M5 的 qlvotube 侧接线已先期落地:mainwindow 内 CXVideoPlayer + cxSetLocaleNumericC。)

## 局限备忘

- 本机无 Qt5 环境:qtavqml(Qt5)仅快照验证,未本机构建试用(后续 CI/装 Qt5 再验)。
- 上游快照不进 CI 编译矩阵;qtav34 构建后续再接入 CI(不并行)。
