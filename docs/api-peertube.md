# PeerTube 后端 API 参考

## 概述

- REST + JSON,BaseURL = `https://<实例域名>/api/v1`;**无需鉴权**即可浏览/搜索/取实例的流(除非需登录的属性;P2 全部匿名)。
- 联邦制:单实例可见本机 + 已关注实例的视频;SepiaSearch(https://sepiasearch.org)是全联邦搜索端点,qlvotube 上的"搜索"用 SepiaSearch 可直接复用同一数据结构。
- 官方 API 文档: https://docs.joinpeertube.org/api-rest-reference.html
- OpenAPI 完整 spec: https://github.com/Chocobozzz/PeerTube/blob/develop/support/doc/api/openapi.yaml

## 实例列表(公共目录)

- 目录与状态页: https://instances.joinpeertube.org (提供 REST: `/api/v1/pickup-instance?search=&sort=-totalInstanceFollowers`,只给通过审核的实例)
- 演示/测试实例(OpenAPI 官方 host):peertube.cpy.re(稳定版)、peertube2.cpy.re(nightly)、peertube3.cpy.re(RC)
- 常见大实例:framatube.org / tilvids.com 等
- 用户配置:`peertube.instances`(默认 framatube.org;UI 支持增删)。

## 浏览/搜索端点

| 端点(GET) | 参数 | 关键响应 |
|---|---|---|
| `/videos` | `count`(≤100)、`start`(**start 分页**,无 nextPageToken,用偏移)、`sort=-views`/`-publishedAt`/`-hot` | `data[]`(Video)、`total` |
| `/videos/shortUUID` | shortUUID(uuid 缩码)或 uuid 或数字 id | VideoDetails(含 `files[]`/`streamingPlaylists[]`/`channel`/`account`) |
| `/video-channels/{handle}/videos` | handle = `channel名@实例` | ResultList,data[] 同 Video |
| `/accounts/{name}/videos` | | 同上 |
| `/search/videos` (SepiaSearch 用本接口:在 https://sepiasearch.org 下狗同样端点) | `search=`、`count`、`start`、`sort`、`tagsOneOf`/`categoryOneOf` 等 | ResultList |
| `/videos/{id}/comments`(+`/threads/{threadId}`) | | Comment 树(P5+) |
| `/instances/{host}` | | 实例信息(rank 友好信息 UI 显示) |
| `/video-categories`、`/video-licences`、`/video-languages` | | 枚举字典(id→label) |

### Video(通用对象)关键字段

- `uuid`(preferred 主键)、`shortUUID`、`name`(标题)、`duration`(秒)
- `views`、`likes`
- `isLocal`(本实例)、`isLive`(直播)
- `publishedAt` / `originallyPublishedAt`
- `category{id,label}`、`language{id,label}`、`licence`、`tags[]`
- `previewPath`、`thumbnailPath`、`bannerPath`(**相对路径,需拼实例域名**;`previewPath` 为 640x 静帧)
- `embedPath`(web 嵌入,桌面端不用)
- `channel{ name, host, avatar{} }` / `account{ name, host }`
- VideoDetails 追加:`descriptionPath`(取全文)、`files[]`、`streamingPlaylists[]`

## 播放(VideoDetails)

**两条路,均可 mpv 直连:**

1. **HLS(大多数实例默认)**
   - `streamingPlaylists[]`:`type=1`(HLS),`playlistUrl`(m3u8 主清单 mpv 直接播)、`files[]`(分段后 files 逐分辨率也提供 fileUrl)、`segmentsSha256Url`(验证用,可忽略)。
2. **WebTorrent/原始文件(实例关闭 HLS 时/或总是额外提供)**
   - 顶层 `files[]`:VideoFile{ `fileUrl`(直链 .mp4)、`resolution{id,label}`(0=audio-only,240~2160)、`fps`、`size`、`magnetUri`/`torrentUrl`(BT 方案,桌面 mpv 不擅长,P2 不用)、`fileDownloadUrl` }。

### StreamInfo 组装

```
videoUrl  = streamingPlaylists[0].playlistUrl (kind=hls)
          或 files[] 里最高 resolution 的 fileUrl (kind=mp4)
quality   = resolution.id
expireSec = 0 (无强过期;CDN 链接一般长期有效)
headers   = [] (PeerTube 无 Referer/UA 强校验;自托管实例个别开启 Basic Auth 时才需要)
```

## 字段映射 → VideoItem

| VideoItem | PeerTube |
|---|---|
| videoId | `uuid` |
| backendInst | 实例域名(列表响应即此实例;SepiaSearch 返回的 item 属于其 `account.host` 不同域名时,resolve 调用应直接用 `fileUrl`(绝对 URL)而不是再查实例 → **detail 请求按 redirect: 用 SepiaSearch 返回的 item 里 detail 相关字段即可**;更简单的做法:搜索结果中的 item 已含 streamingPlaylists 简表,可直接用) |
| title / channel / channelId | `name` / `channel.name@channel.host` / `shortUUID` |
| durationSec / views | `duration` / `views` |
| thumbUrl | `实例域名 + thumbnailPath`(大于 preview 则 previewPath) |
| live | `isLive` |
| nextPageToken | `start + count`(偏移编码为 "s=50") |

## 分页策略

- PeerTube 无 nextpage token:封装 `nextPageToken = "s=" + (start+count)`,适配器内解析偏移即可;`hasMore = start+count < total`。

## 风险/注意

- **同内容跨源**:SepiaSearch 会返回多实例的同一视频(uuid 相同)——VideoItem 以 uuid+host 去重。
- 部分实例启用**实例级鉴权/NSFW 过滤**(401)。列表错误按 ListingPage.error 透出并在 UI 标"该实例受限"。
- `previewPath` 是相对路径;缩略图下载必须与实例域名拼接。
- 隐私:PeerTube 实例一般有 P3P/开源合规声明,无 Google 侧重依赖;但 SepiaSearch 汇聚全部公开视频。

## 附:参考链接

- REST 快速上手: https://docs.joinpeertube.org/api-rest-quick-start
- 全端点 openapi: https://framatube.org/api/v1/... 或 https://docs.joinpeertube.org/api-rest-reference
- SepiaSearch: https://sepiasearch.org
- instances 目录: https://instances.joinpeertube.org
