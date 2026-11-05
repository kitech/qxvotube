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
- 内核探测(M1/M2 启用,见 cxvideo.pri 注释段):pkg-config 探到 libmpv
  定义 `WITH_CX_ENGINE_MPV`,探到 libavformat/libavcodec 定义
  `WITH_CX_ENGINE_FFMPEG`;缺包自动关宏降级(恒编但可条件关)。
- 复用 `qlcomp/compat34.h`(`INCLUDEPATH += ../../qlcomp`,由 pri 自动注入)。
- 构建:`./buildqt3.sh`(Qt3=/opt/qt338sh)/ `./buildqt6.sh`(Qt6=/opt/qt/6.7.3);
  顺序编译、不并行(约定同 qlvotube);产物 `build-qt3*/q3cxv` 等及
  `selftest`(无 GUI 自测,当前 `0 failures`)。

## 里程碑对照(docs/cxvideo-plan.md)

M0 ✅(本骨架:接口可链,Qt3/Qt6 双绿)→ M1 eng_mpv → M2 eng_ffmpeg 软解 →
M3 全功能(字幕/轨道/倍速/EQ)→ M4 硬解 → M5 接入 qlvotube+qskvotube。

## 局限备忘

- 本机无 Qt5 环境:qtavqml(Qt5)仅快照验证,未本机构建试用(后续 CI/装 Qt5 再验)。
- 上游快照不进 CI 编译矩阵;qtav34 构建后续再接入 CI(不并行)。
