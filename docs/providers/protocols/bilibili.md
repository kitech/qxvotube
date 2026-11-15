# bilibili 协议文档(游客口径)

来源:**bilibili-API-collect**(https://github.com/SocialSisterYi/bilibili-API-collect)。
全部 GET REST,无鉴权;解析走 cJSON(qcjson.h)。

## 接口链

| 步骤 | 端点 | 说明 |
|---|---|---|
| 预热 | `GET https://www.bilibili.com/` | 种 buvid3 cookie,缺了搜索 -412 风控 |
| 搜索 | `GET https://api.bilibili.com/x/web-interface/search/all/v2?keyword=&page=&page_size=20` | `data.result[]` 按 `result_type=="video"` 分组,`data[]` 内 `bvid/title/author/mid/pic/duration` |
| 详情 | `GET https://api.bilibili.com/x/web-interface/view?bvid=` | `data.cid`(主分集)/`data.pages[]`(`cid/part/duration`)/`owner.name` |
| 播放 | `GET https://api.bilibili.com/x/player/playurl?bvid=&cid=&qn=64&platform=html5` | `data.durl[].url` 直出 mp4(html5 游客上限 720P) |

## 请求头(风控硬要求)

- `User-Agent`:Chrome 桌面档(`Fingerprint::chromeWeb("zh-CN,en-US")`)
- `Referer: https://www.bilibili.com/`(缺 → -412)
- Cookie:预热 GET 回存(进程内 CookieJar 按域共享);登录用户加 `Cookie: SESSDATA...`

## 风控码 → Err 映射

| REST code | Err | 场景 |
|---|---|---|
| -412 | RateLimit | 无 UA/Referer/高频 |
| -101 | NeedAuth | 需登录 |
| -404 | NotFound | 视频不存在 |
| 其他非 0 | Parse | 信封异常 |

## id 约定

- 搜索/详情入参:`BV...`
- 分集 id:`cid`(数字串);resolve 入参 = 视频 id(单 P 直接 bvid,detail 分集走 extra.bvid+cid)。

## 实测记录(2026-11)

- search 20 条 → detail → resolve 1 个 mp4 流
  (upos-sz-estgoss.bilivideo.com…)→ streamhead Range 0-1M 拉满 1048576 字节。
- 高清 dash 需登录 → `NeedAuth`(no guest streams)。

## 分页(2026-11-15 实测)

- `page`(1 起)+ `page_size=20` 有效;响应 `data.numPages`(实测 50)、`numResults`(实测 1000)。
- hasMore = `page < numPages`;字段缺失时退化为"本页 20 条满即还有"。
- 验收:`tests/paging` → page1 20 条 / page2 20 条 / 跨页重复 0。

## 清晰度(resolveQuality,2026-11-15 实测)

- `platform=html5` 只接受 `qn=16`(360P)/`qn=64`(720P):
  请求 32 回 16,请求 80/112 回 64。
- 映射:360p→16、480p/540p→32(实际降 360P)、720p→64、1080p→80(实际降 720P)。
- 降级时 `Stream.extra.degraded=true` + `requestedQuality`;`Stream.quality`
  取服务端 `data.quality` 的真实档位(360P/720P)。
- 更高清(DASH,需登录)→ 当前不支持,返回的仍是 html5 档。

## 清晰度词汇与就近降级(2026-11-15 补充)

- 全仓统一词汇:`144/160/240/360/480/540/720/1080`(见 `common/quality.h`)。
- html5 只有 360P/720P 两档,映射:`<=360 → qn16`、`480/540 → qn32(实际 360)`、
  `720 → qn64`、`>720 → qn80(实际 720)`。
- 实测:160p/240p/480p → 360P(dedraded);1080p → 720P(degraded)。
- `Stream.extra`:`requestedQuality`、`actualQuality`、`degraded`。

## homeFeed(热门榜,2026-11-15 实测)

- `GET /x/web-interface/popular?pn=<page>&ps=20` → `data.list[]`
  (bvid/title/owner.name/pic/duration/duration)+ `data.no_more`。
- hasMore = `!no_more`;`extra` 带 `aid`/`tname`。
- 验收:`tests/homefeed` → page1 20 条、page2 20 条(dup=0)。

## detail 补全(2026-11-15)

- 主项:`bvid/title/owner.name/owner.mid/pic/duration`,`extra`:
  `desc`/`tname`/`pubdate`/`view`/`cid`(单P兜底用)。
- 分P:`data.pages[]` → `parts[]`,**`part.id = "bvid.cid"` 复合形式**
  (可直接喂 `resolve`/`resolveQuality`),`part.extra` 带 `bvid/cid/page`。
- `pages` 缺失时用 `/x/player/pagelist?bvid=` 兜底。
- 坑:resolve 内部取分P cid 必须读 `extra.cid`,不能用 `part.id`
  (复合形式会喂成非法 cid → `-400 请求错误`)。
