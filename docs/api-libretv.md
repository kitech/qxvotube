# LibreTV 采集站(Apple CMS)后端 API 参考

> 概念澄清:用户所说的 "libretv" 指 **LibreSpark/LibreTV**(自托管聚合站,Next.js,2.2 万 star)。LibreTV 本身不是播放 API 服务端——它是一个收集"Apple CMS V10 采集站接口"+ M3U 并聚合搜索的前端/服务站。因此 qlvotube 的 "LibreTV 后端" = **直接对接 Apple CMS vod 采集接口**(同样的数据体系),并兼容 LibreTV 的源订阅格式;不依赖部署 LibreTV 站点。
> 命名区别:LibreTube(libre-tube/LibreTube, 安卓 YT 前端,走 Piped)与此无关,见 api-youtube.md。

主参考:
- LibreTV Wiki 数据源: https://github.com/LibreSpark/LibreTV/wiki/Data-Sources
- Live-IPTV(远期): https://github.com/LibreSpark/LibreTV/wiki/Live-IPTV

## 一、Apple CMS V10 采集接口

qlvotube 空壳设计:不内置源;用户在设置 → 源管理添加/启停/删除;支持订阅导入(见第三节)。

### 数据源(点播源)字段

```json
{ "name": "示例站点", "url": "https://example.com/api.php/provide/vod",
  "detail": "https://example.com", "isAdult": false, "enabled": true }
```
- `url`:接口地址,**结尾 `/` 自动去除**。
- `detail`:可缺省 —— 部分 CMS 列表接口不带播放地址,需爬详情页(见"详情降级")。
- 源持久化:Config `cms.sources[]`(JSON 数组字符串)。

### 端点(全部 GET,返回 JSON)

| 端点 | 参数 | 响应关键 |
|---|---|---|
| `{api}?ac=list` | `t=分类id`、`pg=` | 分类目录 `class[]` 与 总条数;(部分源 ac=list 返回空,需容错) |
| `{api}?ac=videolist` | `pg`(页 1 起)、`t`(分类)、`month`(可选) | `list[]`(每项含 vod_id,**不含播放地址**通常)、`page`、`pagecount`、`total` |
| `{api}?ac=videolist&wd={kw}` | `wd` 关键词 | 同上,搜索(**源内搜索**;多源并行由 qlvotube 适配器同时打) |
| `{api}?ac=detail&ids={vod_id}` | `ids` 可逗号分隔多个 | `list[]` 含完整详情:`vod_name`、`vod_pic`、`vod_actor`、`vod_director`、`vod_year`、`vod_area`、`vod_remarks`(片尾/更新到 N)、`vod_content`(简介)、`vod_play_from`、**`vod_play_url`** |
| `{api}?ac=detail` | 空 | 最新列表(vod_home 首页推荐用途);

### `vod_play_url` 的解析(核心难点)

形如:
```
第01集$https://xxx.m3u8#第02集$https://yyy.m3u8
```
- 分隔符:集间 `#`,集内 `名称$URL`。
- 可能出现多个播放组(`vod_play_from` 以 `$$$` 分组,如 `线路1$$$线路2`,play_url 按 `$$$` 分组同名序对应)。首版取第一组。
- URL 常见类型:`.m3u8`(HLS,多)/`.mp4`/`.flv`(远期)/`.html`(网页包 → 具体源不可播,打"source 不支持"标记)`;base64 加密的 URI 段(部分隐私源)首版不实现。

### 影片详情 → 多集

- CMS 源以"一部影片 = 多集"为单位(对比其余后端 单视频)。`VideoItem.episodeCount>1` 时详情视图列"选集"。
- `VideoItem.videoId = vod_id`;剧集下标随选集,`episode=idx`(0 起)。

### 字段映射 → VideoItem

| VideoItem | AppleCMS(vodlist action) |
|---|---|
| videoId | `vod_id`(string 化) |
| title / channelId | `vod_name` / 站点源 name |
| thumbUrl | `vod_pic` |
| durationSec | 0(无时长字段;`vod_remarks` 作为备注上屏) |
| views | 0 |
| episode/episodeCount | 由 `vod_play_url` 解析分集计数(列表时 0) |
| nextPageToken | `"pg=%d" % (pg+1)`(pagecount 封顶翻页) |

### 详情页降级爬取(LibreTV 同款)

当 `ac=detail` 的 `vod_play_url` 空/不含 http:
1. GET `{detail}/index.php/vod/detail/id/{vod_id}.html`。
2. 正则提取 `\$https?://...m3u8`(取链接,去 `$` 前缀文本)。
3. 无果,尝试非凡影视特征路径 `/日期/哈希/index.m3u8` 正则。
4. 去重后作为剧集列表(名称"第 N 集"自动生成)。

## 二、多源并行搜索(聚合)

- 对每个 `enabled=true` 的源并行发 `ac=videolist&wd=kw`,超时单条 8 秒/总 10 秒。
- 结果流式合成:单源失败不影响整体,UI 逐条就绪(参考 LibreTV 的"聚合搜索"行为)。
- 每源记健康度(缓存栈):连续失败阶梯停用 30 分钟 → 24 小时 → 长期;可设置页一键恢复(存 video_db 的 sources health 表)。
- 成内容合流:**同 vodId-different-source 不合并**(LibreTV 合并同名同名影片的功能 P5+ 再做),默认一行清爽。
- 成人内容过滤(Config `cms.adult-filter`):`isAdult=true` 的源整体跳过;关键词黑名单(伦理片/福利/里番动漫/无码/SWAG…)标题命中跳过。

## 三、订阅(一键导入源列表)

订阅地址(支持 https 任意静态托管)兼容三种格式,**qlvotube 用同套解析**:

### 1. LibreTV-SourceList JSON(v2)
```json
{ "name": "我的源列表", "version": 2,
  "sources": [ { "name": "示例点播源", "url": "https://bfzyapi.com/api.php/provide/vod",
                 "detail": "https://bfzyapi.com", "isAdult": false } ],
  "liveSources": [ { "name": "示例直播源", "url": "https://example.com/list.m3u", "epg": "..." } ] }
```
- 限制:点播源 ≤100、直播源 ≤50,**qlvotube P3 只导入 `sources`**(M3U/IPTV 属远期)。
- 兼容:老 v1(仅 sources)可用;裸数组格式 `[{name,url},...]` 同样接受。

### 2. 接口目录(source-catalog)
顶层数组全部元素带 `baseUrl` 即按目录解释:`[{ "name": "...", "baseUrl": "https://.../api.php/provide/vod", "enabled": false }]`;归一化(去尾 `/` 与空 `?`/`#`)。

### 3. TVBOX 配置
`sites[]` 中 `type:1`(AppleCMS JSON)条目 → 点播源;省略 type 或 `type:0` 但 url 含 `api.php/provide/vod` → 宽容识别导入(XML 通道 `/at/xml` 排除)。

### 校验与导入规则(照搬 LibreTV)
- 非 http(s)/内网/回环/保留地址静默滤除;
- 按 `url` 去重(先到先得);
- 全部条目不可用时提示原因明细(如"已跳过 3 个条目:已停用 2、地址不可用 1")。

## 四、播放

- CMS 源点播 = **直接 mpv 引 `m3u8`/`mp4` 播放**(HTTP 公网直连,与 PeerTube fileUrl 同铺路,无鉴权)。
- 目标站部分要 UA/Referer:统一带桌面浏览器 UA + `Referer: https://{源域名}/`。
- StreamInfo 组装:
  ```
  kind = hls(m3u8) 或 mp4
  videoUrl = 剧集 url
  expireSec = 0
  headers = ["Referer: https://<源 host>/", "User-Agent: <UA>"]
  ```
- m3u8 内含**广告分片**(夹杂其他 host/上下 ads)首版不做过滤(LibreTV 有 ArtPlayer+hls.js 的做法,GUI 桌面暂用 mpv 直播参数即可)。

## 五、Live 源(M3U;Phase 6,远期)

Free LibreTV 的 liveSources 一套只在 P5 后接,m3u8/flv 解析复用 mpv 天生能力;EPG(XMLTV)UI 更远,不急:
- `GET {m3u}` → `#EXTINF:-1 tvg-id="cctv1" group-title="央视",CCTV-1\nhttps://.../stream.m3u8` 逐行解析改名/分组/链接;
- qlvotube 以"直播"分区呈现(LiveMode 架构另行定稿)。

## 附:常见公开源示例(仅调试试活)

```
公开源地址(实时变动,按空壳惯例不进代码):
  https://360zy.com/api.php/provide/vod         (360=零星源)
  https://bfzyapi.com/api.php/provide/vod       (暴风)
  https://heimuer.tv/api.php/provide/vod        (黑木耳)
  https://apis.jxcxin.com/api.php/provide/vod   (天涯)
(以上全部"实时变动",按 LibreTV 空壳惯例不进代码,仅试活)
```
