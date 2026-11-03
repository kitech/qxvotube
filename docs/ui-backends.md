# qlvotube 后端 UI 布局与逻辑

## 总体布局(沿用现有 qlcomp UI 框架,变更点标 ✚)

```
┌─┬──────────────────────────────┬─────────────────────────────┐
│S│  标题栏(无边框统一)          │                             │
│i├──────────────────────────────┤    ContentView              │
│d│ ✚ 源切换 Tab 条             │   (列表/详情)                │
│e│ ✚ 搜索框(全局,寻 current    ├─────────────────────────────┤
│N│   后端送搜,回车触发)        │  ✚ 视频行: 缩略图/标题/      │
│a│ ✚ 当前源分区列表            │      时长/播放量/频道        │
│v│   - 首页热门                │                             │
│  │   - 各源特有分区(见矩阵)   │                             │
│  │   - 订阅/历史               │                             │
├─┴──────────────────────────────┴─────────────────────────────┤
│ ✚ PlayerBar → 内嵌视频窗(备选:外置独立窗口) + 播控           │
└──────────────────────────────────────────────────────────────┘
```

## 源切换(✚ SideNav 顶部)

- **位置**:SideNav 最顶(用户信息块上方),4 个等宽小 tab:`YT`(YouTube)/`PI`(PeerTube)/`B站`(Bilibili)/`源`(LibreTV CMS 聚合)。
- **切换逻辑**:点击 → `BackendRegistry::setCurrent(id)` → 分区列表整块重建 → ContentView 回"首页分区"→ 首区自动拉第一页。
- **持久化**:Config `lastbackend`(默认 "youtube")。
- **tab 右键**(可选,Phase 3+):跳转当前源设置窗(apin 站源等)。

## 分区矩阵(每源 SideNav 分区列表)

| 后端 | 分区列表(顺序即渲染顺序) | 数据来源 | 无数据时的文案 |
|---|---|---|---|
| YouTube(Piped) | 热门、音乐、订阅、历史、频道库 | `trending?region=xx`、search filter=music_songs、/feed/unauthenticated、本地 db | 订阅为空: "暂无订阅,去频道库添加"  |
| YouTube(官方v3,配了 key 才切出第二个子分区) | 热门(chart=mostPopular)+各省, 分区 categoryId | videos.list | 未配 key: 提示去设置 |
| PeerTube | 本实例热门、订阅实例列表、SepiaSearch 标签页省略(仅搜索用) | /videos、多实例逐个列出 | — |
| Bilibili | 热门、每周必看、动画/游戏/影音(分区 beginner en 子集)、历史 | popular、popular/series、动态(wbi/region)、本地 db | — |
| LibreTV(CMS) | 已启用源列表(每源一分区,点开=源内最新)、多源搜索(专用页) | ac=videolist(默认 t=分类/首页)+per源 | 无源: "设置→源管理 里添加站点" |
| 通用(跨源) | 历史记录、我点过赞 | video_db | 空: 提示 |

- 分区点击 → ContentView 重置滚到 0 → `VideoBackend.listTrending/search 檈分页`.

## ContentView 视图改造(相对现有歌单三视图)

### 视图模式升级
| 原模式 | 新模式 | 数据 |
|---|---|---|
| TracksMode(本地歌单曲目行) | **VideoListMode**(视频行) | ListingPage.items |
| PlaylistListMode(远端歌单卡片) | **ChannelListMode**(订阅的频道行/实例行) | SubscriptionItem |
| CategoryGridMode(分类宫格) | **SourceGridMode**(仅 CMS 后端: 站点宫格 + B 站分区宫格复用) | 每源/categoryId |

### VideoListMode 行布局(全高 ~64ℋ,缩略图左)
```
┌──────────┬───────────────────────────────────┐
│ 缩略图    │ 标题(最多两行截断)                │
│ 16:9     │ 频道名 · 播放量 · 时长 | 发布时间   │
│ (宽度~112)│ [✚ 徽标区: 直播中/4K/分区]        │
└──────────┴───────────────────────────────────┘
```
- 缩略图无缓存前先画 identicon(现有 identicon.cpp 基色衍生),到位重画;名称 sha1(url) 存 cache_fs。
- 触底自动 `nextPage()`(滚位过 85% 触发一次,pending 防重);标题 "加载更多/加载中" 仍透传 m_hasMore/m_loadingNow。

### 详情视图(✚ DetailMode,点击行进入)
```
────────────────────────────
 返回 ‹   [标题]
────────────────────────────
 [视频播放区 16:9]→ 播放中转入播放窗/嵌入
 标题 · 频道 · 播放 · 赞
 [描述(简略) 介绍/简介 折叠展开]
 [选集列表: CMS 多集/ B 站分P] 
 [相关推荐列表(streams.relatedStreams / PT 七天热门)]
```
- 点"播放"→ `resolveStream` → StreamInfo → `PlayerEngine.open`。
- 相关推荐/选集复用 VideoListMode 渲染其子列表。

## 播放区

### 方案(见 backend-abstraction.md PlayerEngine)
- **首选形态**:ContentView 内嵌播放窗(窗口区域内 16:9,QWindow::winId 传给引擎)。Linux X11 下 Qt3/Qt6 均 `--wid`/openExternal 可行;Windows 最后 HWND 嵌入。
- **失败/降级态**:引擎不可用 → 弹"外部播放器播放?"确认 → QProcess 起系统 default(localhost m3u8 亦可下载到临时后 handoff)。
- **PlayerBar 改造**:进度条/播放暂停/音量/全屏/倍速;数据绑定引擎指针(而非 AudioEngineAdapter)。全屏幕 X11 下 `_NET_WM_STATE_FULLSCREEN` 直接发给嵌入窗。

## B 站登录弹窗
- 触发:B 站源 tab 右侧"人"图标(未态)/设置窗账号页。
- 内容:QR 矩阵渲染(纯 C 二维码实现,分 25×25 模块色块,QPixmap 涂色,Qt3 兼容)+ 状态行文字("请用 B 站手机客户端扫码"/"已扫码待确认"/"成功,144小时有效")。

- 轮询:事件 poll 2 秒一次,poll code 86101(未扫)/86090(已扫)/0(成功)/86038(过期重生成)。
- SESSDATA/bili_jct/DedeUserID 存 QSettings34("qlvotube","bilibili");支持设置页"粘贴 cookie" textarea 解析 `SESSDATA=...` 字符串导入。

## 设置窗新增字段(ConfigDialog“通用”页扩展/新增页“视频后端”)

| 字段 | 默认 | 说明 |
|---|---|---|
| lastbackend | youtube | 上次源 |
| piped.instance | https://pipedapi.kavin.rocks | 可逗号分隔多个实例,fallback |
| piped.region | CN | trending region |
| ytapi.key | 空 | 官方 Data API v3 key(配置后启用 v3 分区) |
| peertube.instances | https://framatube.org | 实例多选/多级逗号 |
| cms.sources[] | *(空)* | 采集站 {name,url,detail?,isAdult,enabled} 持久 json |
| cms.subUrl | 空 | LibreTV-SourceList/接口目录/TVBOX 订阅地址 |
| cms.adult-filter | 1 | 成人内容过滤开关(AppleCMS isAdult + 关键词) |
| bili.cookies | 空 | SESSDATA 整段(或扫码登录后自动写) |
| bili.qn | 32 | 清晰度偏好(128 等需 VIP) |
| videoengine | auto | 播放引擎 |
| mpv.path | mpv | 可指定 mpv 可执行文件路径 |

## 交互细节
- 源与 B 站风控:错误以 ContentView 顶横条+左栏分区标题显示 `error` 字符串(不弹窗打断,可"重试"按钮)。搜索空结果与错误区分显示。
- 加载指示:Detail 行 32px "⚠️ 失败/ ⏱ 超时",多源并行伴随逐条就绪(顺序不重排,先到先渲染)。
- 播放量/时长格式化:views 万/亿,hh:mm:ss(qDurationStr helper)。
- 键盘:上下滚动/空格暂停(VideoListMode/DetailMode 才注册,不干扰演示/设置窗)。
- 免责:关于页 + 首次启动一次性提示"内容来自第三方公开接口,由对应数据源负责"。

## 演示(demo.*,无网环境必须可完整操作)
- demo 菜单新增:"demo: 四后端空转界面"(预埋假 VideoItem 数据集,DemoVideoLibrary:每后端 6 条条目,其中 1 条演出 live,1 条 CMS 三集)
- demo: 托盘气泡/demo: 状态栏控件保留。
