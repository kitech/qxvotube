# YouTube 后端 API 参考

qlvotube 中 YouTube 通过**两个适配器**实现:`backend_piped`(主)与 `backend_ytv3`(兜底,需 API key)。

---

## 一、Piped 适配器(主)

### 实例

- 公共实例列表(部署者/用户可自填):官方 `https://pipedapi.kavin.rocks`;LibreTube 客户端同源。实例健康度波动大(2025-2026 年多个实例失联,如 tokhmi.xyz/moomoo.me 等已下线),**务必实现多实例 fallback 与 4 秒超时**。
- 健康监测参考:
  - TeamPiped/piped-uptime 状态页(GitHub README 有实时表)
  - https://piped-instances.kavin.rocks (可拉的实例清单)
- 用户配置:`piped.instance = "url1,url2,..."`(逗号分隔,顺序即 fallback 次序)。

### 端点(全部 GET,返回 JSON,无鉴权)

| 端点 | 参数 | 关键响应字段 |
|---|---|---|
| `/trending` | `region`(如 `US`,`CN`;Regions 枚举见 OpenAPI) | 数组项:`url`("/watch?v=ID")、`title`、`thumbnail`、`uploaderName`、`uploaderUrl`("/channel/ID")、`duration`(秒)、`views`、`uploadedDate` |
| `/search?q=&filter=` | filter:`all`/`videos`/`channels`/`playlists`/`music_songs`/`music_videos`/`music_albums`/`music_playlists`/`music_artists` | `items[]`(含 `type`:"stream"/"channel"/"playlist")、`nextpage`(原串,翻页必需) |
| `/nextpage/search?q=&filter=&nextpage=` | 同上 | 同 /search |
| `/streams/{videoId}` | videoId(11 位) | `title`、`description`、`uploader`、`duration`、`views`、`likes`、`thumbnailUrl`、`hls`(HLS manifest URL)、`dash`(DASH manifest)、`audioStreams[]`/`videoStreams[]`(各含 `url`,`format`,`quality`,`videoOnly`)、`relatedStreams[]`、`livestream`、`subtitles[]` |
| `/channel/{channelId}` | 频道 ID(UC 开头 24 位) | `name`、`avatarUrl`、`subscriberCount`、`nextpage`、`relatedStreams[]` |
| `/c/{name}`、`/user/{username}` | 自定义名 | 同 channel |
| `/nextpage/channel/{channelId}?nextpage=` | | StreamsPage |
| `/comments/{videoId}` (+`/nextpage/...`) | | CommentsPage(首版不做) |
| `/suggestions?query=` | | string [](搜索联想,可选) |
| `/feed/unauthenticated?channels=ID,ID` | 频道 ID 列表 | StreamItem[](无账号订阅聚合) |
| `/login` 等 auth 端点 | | 登录特有的订阅/歌单(P2 不接,Phase 5 视需要) |

### 播放要点

- `/streams` 返回的 `hls` 字段为**经实例代理的 HLS manifest**(也拼 `proxyUrl` 端点),直接可播;DASH manifest URL 亦可引入umu mpv。
- videoStreams/audioStreams 中 `videoOnly=true` 的项是纯视频轨,播放需与音频轨合流(mpv:`video.mp4 --audio-file=audio.m4s` 或直接用 hls/dash manifest,首选后者)。
- 代理域名(piped proxy)是独立域名(GET prio)、与接口域名不同——链接有效期通常约 6 小时,过期重 resolve。
- 502/超时 → 切换下一个实例重试 resolve(列表同理)。

### 字段映射 → VideoItem

| VideoItem 字段 | Piped 来源 |
|---|---|
| videoId | `url` 去 "/watch?v=" 前缀;或 `streams[].url` |
| title | `title` |
| channelId / channel | `uploaderUrl` 后段 / `uploader` |
| durationSec | `duration` |
| views | `views` |
| thumbUrl | `thumbnail` |
| live | `livestream` 或 duration==0 |
| ListingPage.nextToken | `nextpage`(原样透传) |

### 风险与缓解

| 现象 | 处置 |
|---|---|
| 实例 5xx/超时 | 实例轮换;整轮失败 ListingPage.error |
| YouTube 推进反爬致全实例不可用(2025-10 曾集体 502) | useytv3 兜底:提示"官网源不可用,切换官方 API 数据源" |
| 代理流 403 | resolve 再次刷新 |

---

## 二、官方 YouTube Data API v3 适配器(兜底)

### 接入准备

- Google Cloud Console 开 API(v3)、建 OAuth/API key。qlvotube 只用 **API key(无 OAuth)**,属于公开数据只读。
- 配额:免费默认 10000 units/day。**search.list 每次 100 units**(≈100 次搜索/天);`videos.list`、`playlistItems`、`channels.list` 均 1 unit。UI 在设置页提示配额限制。

### 端点(全部 GET `https://www.googleapis.com/youtube/v3`,key=`?key=`)

| 端点 | 用途 | 关键参数 | 响应要点 |
|---|---|---|---|
| `search` | 搜索 | `part=snippet&q=`、`type=video`、`maxResults`(≤50)、`pageToken`、`order`(relevance/date/viewCount)、`relevanceLanguage`/`regionCode` | `items[].id.videoId`、`snippet.title`(HTML 转义)、`channelTitle`、`thumbnails.medium.url`、`nextPageToken`;**无 duration/views**(需二次 videos.list 补) |
| `videos` | 详情/热门 | `part=snippet,statistics,contentDetails,status`、`id=ID,..`(≤50)或 `chart=mostPopular&regionCode=`(1 unit 拉热门,最省) | `snippet.title/thumbnails/publishedAt`、`statistics.viewCount`、`contentDetails.duration`(ISO8601 "PT4M13S" → 需 C++ 解析) |
| `videoCategories.list` | 分区浏览 | `regionCode=`、`part=snippet` | categoryId 可叠加在 videos.list chart 调用 |
| `playlistItems` | 播放列表(Phase 5) | `playlistId=`、`maxResults`、`pageToken` | `contentDetails.videoId` |
| `channels` | 频道详情(Phase 5) | `id=`/`forHandle=` | `snippet.thumbnails`、`statistics.subscriberCount` |

-YT item→VideoItem 映射同 Piped(ISO8601 duration、channelTitle→channel)。compatible with `listing nextPageToken`。
- **播放**:v3 不提供任何流地址。播放一律转 Piped resolve;若 Piped 不可播,再提示用户外部选手(yt-dlp 交给用户自装)。

### 与 Piped 适配器的交互(UI 层)

- 若 `ytapi.key` 配置:YT 分区增加 "官方-热门/官方-分区" 两个子分区,videos.list chart 供热门,search.list 由 v3 走(节省配额则默认源仍 Piped)。
- 配额预计:后台缓存热门 1 小时(1 unit/刷新),搜索强消耗仅在用户确实使用时进 v3。

## 附:参考链接

- Piped API documentation: https://docs.piped.video/docs/api-documentation/
- Piped OpenAPI(spec 完整): https://github.com/TeamPiped/OpenAPI/blob/main/swagger.yaml
- LibreTube PipedApi 接口列表: https://github.com/libre-tube/LibreTube/blob/master/app/src/main/java/com/github/libretube/api/PipedApi.kt
- YouTube Data API v3: https://developers.google.com/youtube/v3/docs
- 配额指引: https://developers.google.com/youtube/v3/determine_quota_cost
