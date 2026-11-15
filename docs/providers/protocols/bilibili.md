# bilibili 协议文档(游客口径)

来源:**bilibili-API-collect**(https://github.com/SocialSisterYi/bilibili-API-collect)。
全部 GET REST,无鉴权;解析走 cJSON(qcjson.h)。

## 接口链

| 步骤 | 端点 | 说明 |
|---|---|---|
| 预热 | `GET https://www.bilibili.com/` | 种 buvid3 cookie,缺了搜索 -412 风控 |
| 搜索 | `GET https://api.bilibili.com/x/web-interface/search/all/v2?keyword=&page=` | `data.result[]` 按 `result_type=="video"` 分组,`data[]` 内 `bvid/title/author/mid/pic/duration` |
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
