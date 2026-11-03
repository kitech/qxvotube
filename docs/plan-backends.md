# qlvotube 多后端总规划

## 目标

qlvotube(Qt3/Qt4/Qt6 三编译全平台可用)同时支持四个视频后端的**列表加载浏览**与**视频播放**:

| 后端 | 定位 | 数据来源 | 播放来源 |
|---|---|---|---|
| YouTube | 国际内容主源 | Piped API(主)+ 官方 Data API v3(兜底,key 可配) | Piped 代理流 / yt-dlp / 官方 API 无播放能力 |
| PeerTube | 联邦制视频,直连最容易 | 实例 REST API + SepiaSearch | `fileUrl` MP4 直连 / HLS(最简单,首个联调后端) |
| LibreTV 源 | 中文影视聚合 | Apple CMS V10 采集站 API(用户自管源)+ LibreTV-SourceList 订阅 | 源返回的 m3u8/mp4 直链 |
| Bilibili | 国内内容主源 | B 站 web API(热门免签;搜索需 WBI) | playurl DASH/MP4,匿名 try_look 试看 720P |

> 注意:qlvotube 沿用"空壳"原则(与 LibreTV 一致):**不内置任何源/频道/片源信息**,只提供对公开接口的请求与播放能力,内容合法性由数据源负责。

## 参考文档(索引)

- `backend-abstraction.md` — 统一封装设计(接口/模型/事件流/播放引擎)
- `ui-backends.md` — UI 布局与逻辑
- `api-youtube.md` / `api-peertube.md` / `api-bilibili.md` / `api-libretv.md` — 各后端 API 参考(端点、字段、鉴权、风险)

资料来源(2026-01 网搜核实):
- Piped: docs.piped.video/docs/api-documentation、TeamPiped/OpenAPI swagger.yaml、piped-uptime 状态页
- PeerTube: docs.joinpeertube.org/api-rest-reference、Chocobozzz/PeerTube openapi.yaml
- Bilibili: SocialSisterYi/bilibili-API-collect(热门/搜索/wbi/playurl/QR 登录)
- LibreTV: LibreSpark/LibreTV wiki(Data-Sources/Live-IPTV)

## 已锁定决策

1. **YT 数据源**:Piped 优先;官方 Data API v3 为可配兜底(搜索 100 配额/次,免费 1 万/天)。
2. **播放引擎**:多引擎策略,跨 Qt3/Qt4/Qt6 全编译平台可用;P1 落 ProcessMpv,后补 LibMpv/QMediaPlayer。详见 backend-abstraction.md。
3. **B 站账号**:匿名(480P+try_look 试看)+ 扫码登录 + 粘贴 cookie,全支持。cookie 持久化在 Config(QSettings34)或 video_db,不进代码。
4. **UI**:侧边栏顶部"源切换" + 分区列表;搜索框全局共通。详见 ui-backends.md。
5. **遵循开发区约定**:不并行编译(不用 `-j`);qlcomp/qlappcom 以符号链接引用;CI 见 .github/workflows/ci.yml。

## 阶段划分

### Phase 0 — 文档(本任务)
- 7 份规划/API 文档落 `docs/`(本目录)。

### Phase 1 — 模型与接口骨架
- `src/videomodel.h`:VideoItem/ChannelInfo/ListingPage 等(替代 musicmodel 的 Track/Playlist 作为主模型;Track 仍供 queuepanel 兼容,逐步收敛)。
- `src/videobackend.h/.cpp`:Backend 接口 + BackendRegistry + 事件回投(复用 EventPoller 泵线程 + ApiResultEvent,类型枚举扩展)。
- `src/playerengine.h/.cpp`:PlayerEngine 抽象 + ProcessMpv 空实现(先不开播,占位)。
- demo:* 演示菜单加"后端切换演示"项,便于无网环境验证 UI。

### Phase 2 — Piped + UI 改造 + 进程播放
- PipedBackend(实例可配,多实例 fallback;默认官方列表 https://piped-instances.kavin.rocks 拉取+本地缓存)。
- SideNav 源切换 tab + 分区矩阵;ContentView 改视频行(缩略图经 cache_fs 缓存)。
- ProcessMpv:QProcess 拉起 mpv(X11 下 `--wid=<窗口id>` 嵌入;win/mac 分别 --wid HWND、NSView 无 wid 时浮窗)。
- 历史记录/播放位置(video_db,sqlite 复用现有 db 层模式)。

### Phase 3 — PeerTube + LibreTV 源管理
- PeerTubeBackend(含实例管理 UI:添加/收藏实例,公共实例列表懒加载)。
- CMSBackend(Apple CMS vod);采集站源管理(添加/启停/测活/订阅导入:LibreTV-SourceList JSON + 接口目录 + TVBOX type:1 宽容识别)。
- m3u8/mp4 直链播放验证;跨源搜索逐源呈现(单源失败不影响整体)。

### Phase 4 — Bilibili 完整接入
- WbiSigner(用现成 md5.cpp;img_key/sub_key 从 nav 接口取,当日缓存)、buvid3 自动获取(首访 bilibili.com 拿 cookie 或随机生成规范化值)。
- 匿名:热门/搜索/播放(try_look=1)。
- 登录弹窗:二维码(QR 码在 Qt3 下用纯 C 写二维码矩阵渲染,Qt4/Qt6 同源)+ poll 轮询(86101→86090→0);SESSDATA 等存 QSettings34("qlvotube","bilibili")。
- cookie 粘贴导入:设置窗"账号"页 textarea。
- 风控处理:-412/-352 提示用户,支持手动填 buvid3。

### Phase 5 — 播放引擎与体验完善
- LibMpv 引擎(可选编译宏,Qt3 X11 winId / Qt6 QWindow::winId)。
- QMediaPlayer 引引擎(Qt6 build 独占,win/mac 上零依赖原生解码)。
- 引擎选择 UI(设置页下拉),失败自动降级链: LibMpv→ProcessMpv→QMediaPlayer。
- 订阅频道(Piped 解析 YT 频道 + B 站 up 主空间),聚合 feed。

## 风险清单

| 风险 | 影响 | 缓解 |
|---|---|---|
| Piped 公共实例健康度剧烈波动(2025 年大量实例失联,YouTube 反爬加剧) | YT 后端不可用 | 多实例轮询+超时降级(4 秒);官方 API v3 兜底;接口层允许用户自填实例 |
| YouTube 对第三方前端 IP 封锁 | Piped 播放 502/403 | 同上;检测 5xx 重试下一个实例 |
| B 站 WBI/风控(-412、-352) | 列表失败 | WBI 签名当日缓存;buvid3 自动+手动两种获取;错误透传 UI |
| B 站 playurl 120 分钟链接有效期 + Referer/UA 校验 | 播放中断/403 | 解析后立即播,se coast;引擎层统一加 Referer/UA headers |
| Apple CMS 采集站质量参差(部分无播放地址需爬详情页、部分需 Referer) | LibreTV 后端部分源不可播 | 详情页降级爬取(参考 LibreTV 正则);源健康度记录,连续失败自动停用(阶梯 30min→24h→长期) |
| libmpv 在 Qt3 构建的编译/嵌入兼容 | 引擎不可用 | 引擎独立可选(--define=-{-DWITH_LIBMPV} 默认关),主路径 ProcessMpv |
| 各后端字段异构(时长/清晰度/点赞/分区) | 数据模型膨胀 | 统一 VideoItem 承载、可空字段+displayExtras[string→string] 扩展 |
| 内容合规 | 法律风险 | 空壳设计;错误说明/免责声明入 README 与 UI 关于页 |

## 与现有代码的关系

- 现有 qlvotube UI 框架(qlmusic 移植)保持:SideNav/ContentView/QueuePanel/PlayerBar 为 UI 骨架;"音乐"语义(Track/Playlist/ncmId)按 ui-backends.md 的字段映射替换为视频语义模型。
- `api.cpp/api.h` 门面不变(HTTP 持续走 EventPoller);后端适配器在其上构建。
- 构建/CI 不受影响:新源文件进 qlvotube.pro HEADERS/SOURCES;不引入新第三方库(视频数据用 cJSON/hjson 解析,列出保持)。
