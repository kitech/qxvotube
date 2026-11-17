# qlvotube UI 接入(视频源网格 / 站点 combo / 动态分类)

2026-11-17 落地，覆盖 `qlvotube`（Qt3/Qt4/Qt6 三档构建）。

## 1. 站点 combo（左上角，用户信息下方）

- 位置：`SideNav` 用户信息块与搜索框之间（24px 行，`label + QComboBox`）。
- 来源：`qxv::createProviders()` 的 provider id 列表；显示名走 i18n `site.<id>`
  （`lang/{zh-CN,zh-TW,en-US}.json`），缺译回退 id 原文。
- 交互：只连 `activated(int)`（Qt3/Qt4 都有，程序填充不触发），
  **索引 → provider id 映射**，不做显示文本匹配。
- Qt3/Qt4/Qt6 同码：`providers.pri` 三档均编译真实源（依赖
  `qlappcom/EventPoller` + libcurl）；`createProviders()` 返回 5 源。
  仅当宿主未接入 providers（空表）时整行隐藏。

## 2. 分类 chips 一律动态拉取

`VideoProvider::categories()` → UI chips → `categoryFeed(key, page)`。
UI 层零站点分类硬编码；无能力时退化为单个「推荐」chip（`key=""` → `homeFeed`）。

| 站点 | 分类来源（动态） | 列表接口 | 现状 |
|---|---|---|---|
| bilibili | `popular` 条目自带的 `tid/tname` 去重排序（30min 缓存） | `newlist?rid=&pn=&ps=` 真分页 | ✅ 实测 12 项 |
| 红果 | 首页 SSR `homeSections[].{tab_type,tab_name}` | 同页按 `tab_type` 过滤 | ✅ 实测 5 项（含新增 AI 剧 tab） |
| 抖音 | 免签接口无分类维度（`board_type` 被忽略、`aweme/positive` 空、`board/list` 404） | — | 走缺省 → 仅「热榜」 |
| 小红书 | `/explore` SSR 探测 `channel_list/channels/channel/homefeedChannel` | `/explore?channel_id=` | 风控/未暴露时 → 仅「推荐」 |
| YouTube | 首页 `ytInitialData` 的 `chipCloudChipRenderer`（text + browse params） | 无 continuation 样本 | 空壳页时 → 仅「推荐」 |

## 3. 网格与翻页

`ContentView::VideoGridMode`：

- 标题区 + 分类 chips（吸顶）+ 响应式网格（单元 ≥200px，16:9 封面）。
- 卡片：封面（异步缓存；未命中先 identicon 占位）、标题单行省略、作者 · 播放量。
- 翻页：底部「加载更多」+ 滚动距底 <120px 自动触发（`time(0)` 1s 节流）。
- 失败：错误条（i18n 文案 + 服务端详情）+「重试」，**不清空已加载卡片**。
- 到底：隐藏 footer；空结果：显示「暂无内容」。

## 4. 封面缓存

`videothumbcache`（`EventPoller`/curl）：同一 URL 只请求一次，失败记账不重试；
Referer 按图床域名推导（B站/小红书/YouTube/字节）；代理沿用 `QXVOTUBE_PROXY`。
Qt3/Qt4/Qt6 同一实现（Qt3 用 `QImage::loadFromData` + `QPixmap::convertFromImage`）；
静态对象改为函数内静态，避免 Qt3 在 QApplication 之前构造 QPixmap。

## 5. 日志

统一用 **Qt 原生日志函数**（不引入封装层）：正常流程 `qDebug`，失败 `qWarning`。

版本差异（同一文件内按版本二选一，均为原生调用）：

| Qt | 形态 | 说明 |
|---|---|---|
| 3.3 | `qDebug("fmt %s", qToUtf8(s).data())` | Qt3 只有 printf 形态；`qDebug(QString)` 会截断中文，禁用 |
| 4.8 / 5.x | 同上（printf） | Qt5 的 printf 走 `QMessageLogger`，保留 `__FILE__/__LINE__` |
| 6.x | `qDebug() << "..." << s` | Qt6 删除 printf 重载，只能流式 |

- `QLVOTUBE_DEBUG=1` → debug 行输出（三档 Qt 都生效）；`qWarning` 恒定输出。
- UI 侧日志点：
  - `providers: N sources (ids)` / `site -> <id>` / `chips: N active <key>`
  - `grid: <site> chip <key> page <n>` / `grid: ... -> <m> cards (more)`
  - `grid error: ...`（warning）/ `categories failed: ...`（warning）
  - `thumb: GET/FAILED`（截图缓存）
- providers 库内部分级日志：`QXVOTUBE_DEBUG="bilibili:info,hongguo:info,http:info"`
  （`setupVideoProviders()` 调用 `dbg::loadFromEnv()` 使模块级开关生效）。
  请求 `Info`、成功 `Debug`、空结果/非 2xx `Warn`、结构失败 `Err`。
- 失败文案：`errortext::i18nKeyFor(Result)`（前）+ `Result.message`（后）拼接。

## 6. 验证

```bash
# providers 离线(分类解析用真实 fixture)
cd providers/build && ctest -E providers_smoke

# 动态分类在线(手动;bilibili/youtube 视网络)
QXVOTUBE_PROXY=http://127.0.0.1:10809 ./providers/build/tests/categories

# Qt3 真编 + providers 接入面
qlvotube/buildqt3.sh
providers/tests/qt3stub/build.sh qt3   # providers=0
providers/tests/qt3stub/build.sh qt6   # providers=5

# Qt6 UI
qlvotube/buildqt6.sh
QLVOTUBE_DEBUG=1 QXVOTUBE_PROXY=http://127.0.0.1:10809 xvfb-run -a ./qlvotube/build-qt6/q6votube
```

## 7. 已知限制

- Qt3/Qt4/Qt6 均启用视频源 UI（三档编真实 providers，2026-11-17 起）。
- 红果 App 取流端点关闭（200+0B），首个空包后熔断 10 分钟直接走 web 360p；
  720p/1080p 需端点恢复（§11.1），其中 720p 还额外需要 bvc2 解码（§11.2）。
- 抖音分类榜需 `a_bogus/Argus` 签名（见 `protocols/bytedance.md` §13）。
- 小红书频道字段名需登录态真实 SSR 才能确认；当前探测不到即不显示。
- YouTube 分类需可达代理（当前出口为空壳页），翻页还需 continuation 样本。

## 8. 2026-11-17 补充修复（Qt3 实测暴露）

- **协议相对封面**：B站搜索接口 `pic` 形如 `//i0.hdslb.com/...`。
  provider 侧补 `https:`（`bili_parse.cpp::picUrl`），封面缓存再做一层 `normUrl` 防御。
- **搜索标题高亮**：B站 `title` 含 `<em class="keyword">`，provider 侧 `stripTags` 剥除。
- **搜索回车**：`SearchLineEdit` 之前只在有联想项时触发；现无联想项时用输入原文触发
  （qlcomp 共享修复，qt3 实测 `search submitted: music`）。
- **触底翻页**：滚轮滚动不经过 scrollbar 的 `valueChanged`，新增 `maybeLoadMore()`
  在 `wheelEvent/onScroll` 共同调用（Qt3 实测 `near bottom -> loadMore` → p2 20 条）。
- **Qt3 静态 QPixmap**：`QMap<QString,QPixmap>` 改为函数内静态，否则
  Qt3 在 QApplication 创建前构造 QPixmap 直接 `qFatal`。
- **红果 App 链条重试噪音**：`hgapp` 的 `multi_video_model` 两处请求
  （`videoModels` 预取 / `videoModel` 单片）把 `retryOnEmpty` 关掉。
  200+空包是服务端风控/端点关闭的稳定结果，重试无意义（Qt3 实测原为
  1 秒内 6 次 POST + `retry 2/2`/`3/3` 两条 Warn），现只发 1 次；
  `maxAttempts` 仍保留，仅对传输错误/5xx 生效。
- **日志排版**：`media net:` 改为 `referer='%s' ua=%d proxy='%s'`，
  空 referer 时显示 `referer=''` 而不是双空格（Qt6 分支同字段名）。

## 9. 播放直链网络选项（2026-11-17 追加）

B站等站点的媒体直链绑定「请求出口 IP + Referer/UA」，缺一即 403。
本轮把选项打通到播放引擎：

- `cxengine.h`：新增 `CxNetOptions{proxy, referer, userAgent}` 与
  `CXEngine::setNetOptions()`（默认空实现，不影响既有宿主）。
- `eng_ffmpeg`：`av_dict_set("headers", "Referer/User-Agent", …)` +
  `av_dict_set("http_proxy", …)` 后 `avformat_open_input`。
- `eng_mpv`：`loadfile` 前设置 `http-proxy` / `http-header-fields`（含 Quick 二段起播）。
- `CXVideoPlayer::setNetOptions()`：`setEngine` 之后自动透传。
- providers 补元数据：bilibili/红果/YouTube/小红书 的 `Stream.extra` 提供
  `referer` / `userAgent`；UI 在播放前组装 `CxNetOptions`，
  未显式指定 proxy 时沿用 `QXVOTUBE_PROXY`（与 API 同出口）。

Qt3 实测：`pos=0→29000ms` 连续推进，无 `HTTP error 403`；Qt4 同样通过。

## 10. hg\* / localstream / yt-dlp 的 Qt3/Qt4 移植（2026-11-17）

| 模块 | 关键改法 |
|---|---|
| `hgcenc` | `quint*`→`uint*_t`；`QVector`→`std::vector`；`QByteArray` 切片走 `qbaMid/qIndexOf/qbaFromData` |
| `hgapp` | `QUrlQuery` 用 qlcomp 同名实现；`currentMSecsSinceEpoch`→`wallMs`；`toUtf8/constData`→`qToUtf8/qba*` |
| `hgstream`/`localstream` | 保留 httplib（纯 C++）；`nowMs`→`wallMs`；`fromHex/replace` 本地实现 |
| `ytdlp` | `QStandardPaths`→PATH 扫描；`QProcess` 双分支（Qt3 `readyReadStdout/processExited`，Qt4+ `readyReadStandardOutput/finished`）；`YtdlpProc` 移入头文件供 moc |

验证：
- Qt3 `providers/tests/qt3sign/` 新增第三 target `qt3hgc`（spade KAT + mp4box 空输入 + 链接 ytdlp/localstream），all green；
- Qt3 红果实测：`device_register` 200 → `multi_video_model` 请求发出 → 服务端空包时显式
  `app chain failed(空响应/风控) → web fallback`，web 360p 正常播放（pos 连续推进）。
- Qt3 运行级验证（qt3hgc）：
  - `localstream` 真实起服务 → `Range: bytes=2-5` 返回 **206 + "2345"**；
  - `ytdlp::fetch`（`QXVOTUBE_TEST_YTDLP=<url>` 开启）→ `formats>0`（Qt3 QProcess 分支 + PATH 扫描 + 代理）。

## 11. 外部阻塞调研结论（2026-11-17 网搜核对 + 本机实测）

### 11.1 红果 `multi_video_model` 200+空包：已确认是服务端关闭，不是本地参数问题

社区同源案例：`waligoraamodio288-rgb/hongguo-desktop-releases` issue **#742**（2026-10-21，已关闭）。
作者给出的定位与我们的现象完全一致，且踩到了同一个坑：

> `/multi_video_model/v1/`、`/video_model/v1/`、`/multi_video_model/preload/v1/` 在签名/配置正确时
> 全部 **HTTP 200 + 0 字节**；同期 `multi_video_detail/v1/` 正常 → 是播放类接口对「审计游客」
> 身份静默返回空。换成公开游客画像（`7.3.5.32 / channel=update_64 / 无签名`）后旧路径恢复 5 档轨道；
> **preload 路径在两种身份下都是 0 字节，不要按「改路径」修**。

我们据此取到对方参考实现 `desktop_public_player.py`，与 `providers/common/hgapp` 逐项对比：

| 项 | 参考实现（当时可用） | 我们的实现 |
|---|---|---|
| 版本画像 | 73532 / 7.3.5.32 / update_64 | 一致 |
| 签名 | 无 | 一致 |
| device_id / iid | 随机 **19 位数字**（`10^18~9·10^18`） | `device_register` 返回的 16 位 |
| 标识字段名 | `iid` | `install_id` |
| openudid / cdid | **不发送** | 发 `hglive0000000001` / `hglivecdid…`（占位假值） |
| `_rticket` | 仅多集接口带 | 两个都带 |
| 空 200 处理 | 原请求重试 3 次，间隔 0/5/12s | 不重试（见 §8） |

**2026-11-17 本机对照实测**（真实 vid `7704532501426867224`，取自 detail 页 `vid_list`）：

| 变体 | 结果 |
|---|---|
| 参考画像（19 位 id + `iid` + 原版 UA/headers + 最小 body） | 200 / **0 B** |
| 同上 + `dr_scene=preload` + `biz_param` | 200 / **0 B** |
| 单集 `/video_model/v1/` | 200 / **0 B** |
| 走代理 vs 直连 | 都是 200 / **0 B** |
| 再补 `passport-sdk-version` / `x-tt-store-region` / `openudid` / `cdid` | 200 / **0 B** |

响应头确认是边缘节点主动给空体（不是压缩/解析问题）：
`HTTP/2 200`、`content-type: application/json`、`content-length: 0`、`server: TLB`、带 `x-tt-logid`。
旁证：该仓库 2026-11 的 issue #747、#918 仍是「播放失败 / 进度条不动」。

**结论**：社区 10-21 的修复在 11 月已失效，端点对匿名/游客身份再次全空。我们不改协议参数
（改了也无法验证，属无依据改动），改为**熔断 + 兜底**：

- `hgapp` 首个空包后熔断 10 分钟（`providers/common/hgapp/hgapp.cpp` 的 `appBlocked/appBlockTrip`）；
  熔断期内 `videoModels/videoModel` 直接返回失败，连 `device_register` 都不发，provider 立即走
  web 360p 兜底。Qt3 实测（连播 3 集）：第 1 集 `multi_video_model` 2 次 POST 后熔断，
  第 2 集 **0 次**、第 3 集 **0 次**；`fallback-play-info` 直接 200（1.7~5.3s）并正常起播。
- `QXVOTUBE_HG_APP_PROBE=1` 时熔断期内放行**一次**探针请求（不重试）：实测第 2 集
  POST 计数 2→3（只放行 1 次）、同批并发的单集请求同时被跳过，探针再空不重复告警，
  第 3 集恢复为 0 次。拿到轨道时会自动解锁。用于端点将来恢复时无需改代码即可发现。
- 恢复时优先核对两处历史差异：标识字段名 `install_id` vs 参考实现的 `iid`、以及 `openudid/cdid`
  的 `hglive…` 占位值是否会被判为异常画像。

### 11.2 红果 720p 的 bytevc2 不是死路（1080p 本来就不需要）

- 参考实现与 issue #742 实测：**1080p = bytevc1**（解密重封装后是标准 HEVC，普通 ffmpeg/PyAV 可解）；
  **360p~720p = bytevc2**（字节私有编码，≈VVC 衍生）。
- `wxyzh/ffmpeg-bvc2` 提供现成解码：基于 jellyfin-ffmpeg，叠加约 **107 行 patch** 把 librempeg 的
  bvc2 支持移植过来——`isom_tags` 映射 `bvc2` 四字码 + 新增 `cbs_type_bvc2`（按 4 字节长度前缀
  切 NAL，不做 VVCC 解析）+ 一个 `ff_bvc2_decoder` 入口，实际解码复用上游 `libavcodec/vvc/*`。
  许可 **GPLv3**（bvc2 代码来自 librempeg，且含 x264）。
- 因此我们的策略不变、也不需要改码：`Track::playable()` 只认 bytevc1、`pickBest()` 优先 bytevc1
  （最高档即可播）。若将来要支持 720p 降档，需要换用带 bvc2 的 ffmpeg 构建并承担 GPLv3 分发义务，
  不引入到默认依赖里。

### 11.3 其余三项维持原判（补充实测证据）

| 阻塞 | 结论 | 处置 |
|---|---|---|
| 抖音 `a_bogus` | `ylcangel/a_bogus` 明确「**只提供 js 版本**，其他语言请自行实现」；其余教程均为补环境 / JSVMP 还原 | 暂不移植（纯 C++ 需移植 JS VM 或重写 SM3+VM 逻辑），分类榜保持 Unsupported |
| 小红书频道 | 官方开放平台只开放 OAuth + `basic_info`；频道/笔记列表仅第三方付费 API（TikHub / Rnote / EveryInfra）或登录态 SSR | 维持 SSR 探测，拿不到即 Unsupported；等 cookie 再补真实验证 |
| YouTube 分类/翻页 | 无服务端分类列表 API；实测当前出口下 ANDROID `browse FEwhat_to_watch` 也回新 UI 壳（0 renderer），`FEtrending` 400。追测 `yt-dlp 2026.10.26`：`feed/trending` 直接报 `redirected to youtube.com home page`；频道页与 `ytsearch10` 可用但耗时 **13.6s/10 条**，做列表兜底不划算 | 维持「仅推荐」，搜索走自有 innertube；不引入 yt-dlp 作为列表兜底 |

> 备注：红果媒体直链的 Referer 结论仍是**第三方资料 + 代码路径自证**（去掉 Referer 后 web 360p
> 可播；带 Referer 的 403 未能在本机复现到同一 CDN 域名），注释里保留了来源与适用范围。

## 12. 播放信息 / 右键菜单 / 默认分辨率（2026-11-17，M7）

### 12.1 默认播放分辨率
- 配置 `video.quality`（设置 → 播放分辨率），默认 **360P**；`auto` = 各源默认。
- 环境变量 `QLVOTUBE_QUALITY` 可覆盖（排障）。
- 生效路径：`playVideoAt()` 调 `resolveQuality(id, q)`；`auto` 时回落 `resolve()`。
- **降级如实上报**：provider 通过 `Stream.extra.degraded=true` + `requestedQuality`
  告知，状态栏提示"请求 1080P，实际 720P（该源上限）"。
  - bilibili 游客上限 720P；youtube 360P 直走 itag18 渐进流；douyin 未实现 `resolveQuality`（回落源默认）。

### 12.2 播放器顶栏（实时分辨率）
- `CXEngine::videoInfo()` → `CxVideoInfo{url,width,height,codec,hw,bitrateKbps,fps,quality,...}`，
  经 `CXVideoPlayer::videoInfoChanged()` 通知 UI。
- ffmpeg 引擎：宽高/codec **实时取解码器**（硬解失败会回落，不能快照），码率取
  `AVFormatContext/bit_rate`，fps 取 `avg_frame_rate`。
- libmpv：`video-params/w|h`、`video-codec`、`hwdec-current`、`container-fps`；码率无属性 → 0（UI 显示 N/A）。
- 顶栏显示 `640×360 · 360P`；取不到显示 `--`，不编造。

### 12.3 右键菜单（列表卡片 / 播放器）
- 列表卡片：复制网页地址 / 复制直链(注明会过期) / 显示地址 / **在浏览器打开**（`qOpenUrl`）。
- 播放器：视频详情… / 复制直链 / 在浏览器打开。
- 列表右键**不再触发播放**（`mousePressEvent` 只处理左键）。
- 直链含签名，换设备/过期会 403——菜单文案已注明，不做可播承诺。

### 12.4 列表卡片信息
- 封面右下角：**时长徽标**（`VideoItem.durationMs`）。
- 作者行追加：**发布日期**（`VideoItem.publishMs`；0 = 不显示）。
- bilibili 填 `pubdate` + `webUrl`；youtube/hongguo/xhs 填 `webUrl`（发布时间形态不定，取不到留 0）。

### 12.5 视频详情弹窗
- 字段：分辨率 / 码流 / 解码器 / 硬解（如实显示"软解"）/ 帧率 / 档位（含降级说明）/ 地址（超长截断，可一键复制完整）。
