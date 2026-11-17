# 视频输出后端（vo）规范

状态：2026-11-17 定稿并落地 P1（`renderer_base` / `x11` / `sdl` + 工厂 + 配置页）。
适用范围：**qlvotube 的 ffmpeg 播放路**（`CXEngineFfmpeg` → `CXVideoRenderer`）。
mpv 相关代码本规范**不涉及、不修改**；mpv 的 vo 清单只作命名/语义参照。

## 1. 后端与平台

| 选项 | 实现 | X11 | Wayland | Windows | macOS | 依赖 |
|---|---|---|---|---|---|---|
| `auto`（默认） | 平台链 | ✅ | ✅ | ✅ | ✅ | — |
| `xv` | `renderer_xv` | ✅ | ✗ | ✗ | ✗ | `-lXv` + 可用 Xv adaptor |
| `x11` | `renderer_x11` | ✅ | ✗ | ✗ | ✗ | `-lXext`（MIT-SHM，失败回退 `XPutImage`） |
| `sdl` | `renderer_sdl` | ✅ | ✅ | ✅ | ✅ | SDL2 **或** SDL3（二选一） |
| `raster` | `renderer_software` | ✅ | ✅ | ✅ | ✅ | 无 |
| `d3d11` | `renderer_d3d11` | ✗ | ✗ | ✅ | ✗ | `-ld3d11 -ldxgi` |
| `metal` | `renderer_metal` | ✗ | ✗ | ✗ | ✅ | Metal/QuartzCore/AppKit |
| `gl` | `renderer_gl` | ✅ | ✅ | ✅ | ⚠️ 已废弃 | `-lGL` + Qt opengl 模块 |

非当前平台的选项不会进 `auto` 链；显式选择时若不可用，回退 `raster` 并写日志。

## 2. `auto` 链（GL 永远最后）

```
Linux/X11     : xv → x11 → d3d11(无) → metal(无) → sdl → raster → gl
Linux/Wayland : sdl → raster → gl
Windows       : d3d11 → sdl → raster → gl
macOS         : metal → sdl → raster → gl
```
（链由编译期 `WITH_CX_RENDERER_*` 决定，`cxAvailableVideoOuts()` 返回实际可用项。）

判定：渲染器构造成功（`isAvailable()`/`ready()`）；首帧到达后生效。
日志：`video out: x11 (auto:xv 不可用)`（`QLVOTUBE_DEBUG=1`）。

## 3. SDL2 / SDL3

- 两者导出**同名符号**（ABI 不兼容），**不可同时链接**。构建期二选一：
  `pkg-config sdl3` 优先 → 否则 `sdl2`；`QXVOTUBE_SDL=2|3` 可强制。
- 差异收敛在 `cxsdl_compat.h` + `cxsdl_audio.cpp`（唯一出现 `#if CX_SDL_MAJOR` 的地方）：
  - 嵌入原生窗口：SDL2 `SDL_CreateWindowFrom`；SDL3 `SDL_CreateWindowWithProperties`
    + `SDL_PROP_WINDOW_CREATE_{X11_WINDOW_NUMBER,WIN32_HWND_POINTER,COCOA_VIEW_POINTER}`。
  - 渲染器：SDL2 `ACCELERATED|PRESENTVSYNC` + `RenderCopy`；
    SDL3 `CreateRenderer` + `SetRenderVSync` + `RenderTexture`。
  - 音频：SDL2 `OpenAudioDevice`+`QueueAudio`；SDL3 `OpenAudioDeviceStream`+`PutAudioStreamData`。
- **SDL 不保证无 GL**：后端可能是 `opengl/direct3d11/metal/software`，
  `CXRendererSdl::backendName()` 取自 `SDL_GetRendererName`/`SDL_GetRendererInfo`，
  日志与文档都按实际值写。
- **SDL3 音频暂禁用**：本机（Qt3 宿主）实测 `CX_FF_AO=sdl` 在 SDL3 下推流阶段整进程卡死
  （独立小程序同样的 API 正常，宿主内不行）；`cffaoNewSdl()` 在 `CX_SDL_MAJOR==3` 时返回 0，
  由 `cffaoCreate` 落 `silent` 兜底。SDL3 仅用于**视频**输出；SDL2 构建下音频行为不变。
  排查结论：音频 `SDL_OpenAudioDeviceStream` 与 Qt3 宿主组合疑似线程/事件互锁，
  待有真机 SDL3 环境再验（不阻塞视频后端交付）。

## 4. 配置与日志

- 优先级：`QLVOTUBE_VO_OUT`（env） > `config.json : video.out` > `auto`
- 配置页：设置 → 视频输出（`SelectConfigItem("video/out")`），选项文本即后端名；
  改动在**下次启动**生效（后端在启动时挂到 `VideoPane::surface()`）。
- 日志（`QLVOTUBE_DEBUG=1`）：`video out: x11 (auto:xv 不可用)`

## 5. 能力差异（如实标注）

| 能力 | `gl` | `x11` / `xv` / `sdl` / `raster` |
|---|---|---|
| 亮度/对比度/饱和度 | GL shader | CPU LUT（`cxEqApplyRgb`，各后端共用 `renderer_base`） |
| **色相（hue）** | ✅ | ❌ 无（既有边界，EQ 对话框需注明） |
| 缩放 | GPU 双线性 | CPU `smoothScale`；`xv` 由 X server 缩放 |
| 色彩管理 / HDR | ✅ | ❌ |
| 截图 | ✅ | ✅（LUT 后所见即所得） |
| 1080p CPU | 低 | 中（每帧一次缩放 + 提交，需实测） |

mpv 官方也把 `gpu/gpu-next` 之外的 vo 标为"兼容/特殊用途"。我们设 `auto` 为默认是
**产品取舍**（无 GL 环境可用），不是性能更优。

## 6. 实现约束（踩坑记录）

| 后端 | 约束 |
|---|---|
| 全部 | **Qt3 的 32bpp 缩放坑**：`QImage::smoothScale()`/`scale()` 以及 `QPainter::scale()` 变换在本机 Qt 3.3.8 下会把 32bpp 画面画成纯黑（未缩放的 `drawImage` 正常，QImage 存盘也正常）。因此缩放统一走 `cxScaleRgb()`（libswscale，RGB32→RGB32，`renderer_base`），已验证正确。 |
| 全部 | Qt3 的 `QImage(w,h,32)` 带 alpha 通道：解码只写 RGB、alpha 字节为 0，`drawImage` 会当全透明（黑屏）。`cxframe.h::cxNewImage` 已 `setAlphaBuffer(false)`。 |
| 全部 | **不要在构造期取 `winId()`**：Qt3 下子件尚未 reparent/show 时取 `winId()` 会把它变成独立顶层窗，随后 SDL 包装报 `X Error: BadWindow`。X11/Xv/SDL 后端一律在首帧 `present()` 惰性建原生资源。 |
| `x11` | SHM 生命周期（`XShmCreateImage`→`XShmAttach`→`XShmDetach`+`shmdt`+`shmctl`）；双缓冲 + `ShmCompletion` 计数（`send_event=True`）；SHM 不可用**整组**回退普通 `XImage`；Qt 侧不绘制（Qt3 `setBackgroundMode(NoBackground)` + 构造 flags `WNoAutoErase`，Qt5+ `WA_PaintOnScreen`+`WA_NativeWindow`） |
| `xv` | `XvGrabPort`/`XvUngrabPort`；格式优先 YUY2 再 UYVY；`XvCreateImage(...,0,w,h)` 后按 `data_size` 自备缓冲；**合成窗口管理器下可能黑屏**（日志提示换 `x11`/`gl`）；letterbox 交给 `XvPutImage` 目标矩形 |
| `sdl` | `SDL_CreateWindowFrom`/properties 包装宿主窗口；Qt 侧不绘制；backend 名进日志 |
| `d3d11` | `D3D11_CREATE_DEVICE_BGRA_SUPPORT`；HARDWARE→WARP；`DXGI_SWAP_EFFECT_DISCARD` + 单缓冲才能 `CopyResource` 进后台缓冲；`ResizeBuffers` 前释放引用；Qt 必须停止绘制否则覆盖 D3D 内容；DPI 用 `devicePixelRatio` |
| `metal` | `CAMetalLayer` 挂 `NSView`（`QWidget::winId()`），`framebufferOnly=NO` 才能 blit；`contentsScale=backingScaleFactor`；Qt5+ 限定；`.mm` 走 `OBJECTIVE_SOURCES` |

## 7. 实测边界（禁止升级为"支持"）

2026-11-17 本机（Linux 5.x / X.Org 21.1.25 / Xvfb `:99` 1280x800x24，无 Xv adaptor；Qt 3.3.8 主 + Qt 4.8 冒烟）：

| 平台 | 状态 |
|---|---|
| Linux/X11 | **实测**：默认 `auto` → `x11 (auto:xv 不可用)` 出画；`QLVOTUBE_VO_OUT=x11/raster/sdl/gl` 逐个复查均出画；`xv` 显式选择时回退 `raster` 且日志写明原因；配置页选择写入 `config.json: video.out`，重启日志 `video out: x11 (config/env)` |
| Linux/Wayland | **未实测**（无会话）；只保证编译期条件成立、X11 下不会被选中 |
| Windows | **未实测**：`d3d11`/`sdl` 代码未在本机编译 |
| macOS | **未实测**：`metal`/`sdl` 代码未在本机编译 |

构建矩阵（本机）：`WITH_CX_RENDERER_X11`/`_XV`/`_SDL` + `CX_SDL_MAJOR=3`（链接 `libSDL3.so.0`，未见 `libSDL2`），GL 段保持原样。
Qt4 `buildqt4.sh` 0 error（冒烟，未跑界面）。

## 8. 不做

`wlshm`（Wayland 下 `sdl`/`raster` 已覆盖）、`drm`（独占 KMS）、
`vaapi`/`vdpau`（硬件覆盖收益被 `xv`/`gl` 覆盖）、
`tct`/`sixel`/`kitty`/`caca`（终端输出）、
`dmabuf-wayland`（GPU 路径，与"无 GL"目标相反）。

## 附：许可证边界

mpv 为 GPLv2+（部分 LGPL），其 `vo_x11.c` / `vo_xv.c` / `vo_direct3d.c` **不得拷贝**。
本实现仅依据 Xlib/XV/DXGI/Metal 官方 API 文档与公开示例。
