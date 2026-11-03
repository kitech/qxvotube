# Bilibili 后端 API 参考

主参考仓库:**SocialSisterYi/bilibili-API-collect**(需 wbi 的接口 2023-03 起逐步强制):
- 热门: docs/video_ranking/popular.html
- 搜索: docs/search/search_request.md / hot.md
- 播放流: docs/video/videostream_url.md
- WBI 签名: docs/misc/sign/wbi.md
- 扫码登录: docs/login/login_action/QR.md
- 视频信息: docs/video/info.md

约定:除特殊说明,全部 **GET**,Base=`https://api.bilibili.com`;响应统一包:`{code, message, ttl, data}`;`code!=0` 即错误(-412 风控/-352 风控/-101 账号未登录/-404 未找到)。强制 https。绝大多数请求需 **UA + Referer**(`https://www.bilibili.com/`),否则可能风控。

## 请求基础(适配器内约定)

| 要求 | 值 |
|---|---|
| UA | 现代桌面浏览器 UA(`Mozilla/5.0 $(平台) AppleWebKit/537.36 ...`) |
| Referer | `https://www.bilibili.com/` |
| Cookie | 匿名:buvid3(必须,否则搜索直接 -412);登录:SESSDATA/bili_jct 等 |
| platform | web(默认回指 pc)|

## buvid3 获取(首次)

方案 A(稳):curl GET `https://www.bilibili.com/`(拿 set-cookie 里的 buvid3/buvid4,存 QSettings34)。
方案 B(备用):本地随机生成规范化 cookie 值手动填入(设置页提供"粘贴 cookie"兼容它)。
时效:长期有效,持久化;失效即重取(方案 A 重建)。

## WBI 签名(搜索/分区等接口必需)

1. GET `https://api.bilibili.com/x/web-interface/nav`(匿名也可):`data.wbi_img.img_url` 与 `sub_url`,取 URL 文件名部分得 `img_key`、`sub_key`(形如 32 hex)。
2. `mixin_key` = 按 `MIXIN_KEY_ENC_TAB`(64 位下标置换表,固定常量,入源码)重排(img_key+sub_key)后**取前 32 字符**。
3. 请求参数追加 `wts` = 当前秒级时间戳;所有参数按 key 字典序排序,URL-encode(`%20` 空格 + uppercase 十六进制对齐);拼接 `mixin_key`,整体 **MD5 → w_rid**。
4. 最终 query = 原参数 + `wts` + `w_rid`。
- img_key/sub_key **每日变更,建议当日缓存**(键随 QSettings34 持久;跨日重取)。
- 实现:复用 `src/md5.cpp`,新增 `src/wbisign.{h,cpp}`(纯 C 字符串逻辑,Qt3 兼容)。

## 列表端点

| 端点 | 鉴权 | 关键参数 | data 关键字段 |
|---|---|---|---|
| `/x/web-interface/popular` | 免签(带 cookie 则个性化) | `pn`(页码,默认1)、`ps`(每页,默认20) | `data.list[]`、`data.no_more` |
| `/x/web-interface/popular/series/list` | 免签(可能 -352) | — | 每周必看列表(点进 `popular/series/one?number=`) |
| `/x/web-interface/popular/series/one` | 免签 | `number`(系列号)、`pn`/`ps` | 视频列表 |
| `/x/web-interface/dynamic/region` | 免签(旧接口) | `rid`(分区 id: 1=动画 …, 全表入源码)、`pn`、`ps` | `data.archives[]`、`no_more` |
| `/x/web-interface/wbi/view` 或 `/x/web-interface/view` | wbi 或 免签(限流) | `bvid` 或 `aid` | `data.title`、`data.owner{mid,name,face}`、`data.videos[]`(**分P,每项 cid** ~)、`data.pages[]{cid,part}`、`data.pic`(缩略)、`data.stat{view,like,...}` |
| `/x/web-interface/wbi/search/all/v2` | wbi + buvid3 cookie | `keyword`(必需) | `data.result[]`(带 `result_type`:"video"/"user"/"live_room"…;视频中 bvid/作者/duration 含中文如 "5:30"/play) |
| `/x/web-interface/wbi/search/type` | wbi | `keyword`、`search_type=video`、`page`、`order`(totalrank/pubdate/click)/`category_id` | 同上单类 |
| `/x/web-interface/wbi/search/square` 与 `/wbi/search/default` | wbi | `limit`、`platform=web` | 热搜词/默认词(搜索框占位) |
| `/x/web-interface/suggest` | 免签 | `term=` | 搜索联想 |
| 用户空间(Phase 5) | wbi | `/x/space/wbi/arc/search?mid=&ps=&pn=&order=` | UP 投稿列表 |

注意:**热门接口不带 cookie 也会拉个性化排行**(整体: 全局热门),is_no_more 翻页判断标 `data.no_more`。

## 播放(2 步)

### 1. 详情拿 cid
`/x/web-interface/wbi/view?bvid=...`(或免签名 `/x/web-interface/view?bvid=`;限流较严)。
多 P 视频:`data.pages[]` 选取(默认第1 p)。

### 2. playurl 解析流
`GET https://api.bilibili.com/x/player/wbi/playurl`

| 参数 | 说明 |
|---|---|
| `avid` 或 `bvid` | 视频主键 |
| `cid` | 分P id(上文) |
| `qn` | 清晰度:16=360P 32=480P 64=720P(需登录) 80=1080P(需 VIP 部分时代峰值) 116=1080P60(VIP) 120=4K(VIP) 等 |
| `fnval` | 格式 bitmask:16=DASH(推荐)、1=MP4(durl);flash 已弃 |
| `fourk` | 4K 允许 |
| `platform` | `pc`(默认)|`html5` |
| `try_look` | **匿名试看:1**(720P 最高,大文件限3分钟;供未登录) |
| `session` | md5(buvid3+时间戳),可选 |

响应:
- `dash{ video[], audio[]{ base_url, backup_url[], bandwidth, codecs, id } }` → kind="separate"(DASH 分轨,**不提供 manifest,需要 mpv --audio-file 合流**)。codecid:7=AVC / 12=HEVC / 13=AV1(qnv3/T 续 codecid select 中文)。
- `durl[]`(fnval=1 旧路)→ kind="mp4"。
- `quality`、`timelength`(毫秒时长)、`support_formats`(UI 清晰度切换可选表)。
- filter: **quality=接受 list**,`durl[0].url` 大部分单 mp4 url。

### StreamInfo 组装(适配器)

```
kind      = dash 有 → "separate" (videoUrl=dash.video[codecid=7][idx], audioUrl=dash.audio[idx])
            或 durl → "mp4"
quality   = qn(实际)
expireSec = 7200      // B 站 playurl 有效 120 分钟
headers   = ["Referer: https://www.bilibili.com/", "User-Agent: <同 UA>"]
```

引擎(mpv)侧:统一附加 headers;`separate` → 主视频 + `--audio-file=音频 base_url`。

## 账号(三档,目标全支持)

### a) 匿名
- 可用:热门、每周、动态分区、view 详情(限流)、search/all/v2(带 buvid3)。
- 播放:`try_look=1` → 最高 720P(1080P 尝试偶发,见 DeepWiki 认证门矩阵);内侧注意 try_look 大文件3分钟限制提示。

### b) 扫码登录(QR)
```
POST/GET https://passport.bilibili.com/x/passport-login/web/qrcode/generate
  → data.url(拼二维码内容) + data.qrcode_key(32字符)
GET  .../web/qrcode/poll?qrcode_key=xxx
  data.code: 86101 未扫 / 86090 已扫待确认 / 0 成功 / 86038 过期(180 秒需重生成)
  成功后 Set-Cookie: SESSDATA(有效期 1+ 月, wbi 换发机制)、bili_jct、DedeUserID、DedeUserID__ckMd5 + data.refresh_token
```
- UI 二维码渲染(data.url 做内容)、轮询 2s。
- cookie 写 QSettings34("qlvotube","bilibili");`wts`/`w_rid` 内容照常。
- Tips:Set-Cookie 须由 curl 存下(EventPoller 记录多个响应头集合,/parser set-cookie 处理)。

### c) 粘贴 cookie 字符串
- 设置窗账号页 textarea:仅要求含 `SESSDATA=...(URL decode);bili_jct=..;(可选)`剪贴板行。直接加载至 Jar。
- 检验:GET `/x/web-interface/nav` 看 `data.isLogin`(true)/`data.money`。

### vip 差异(设置页可选)
- 用户带 vip 的 bili_jct:qn 高达 116/120;四码 fnval 加位 `128|fourk=1`。
- 大权限 cookie 或风控:后台 fail-safe 回退到匿名 + 提示"登录态失效,回退试看"。

## 风控矩阵

| code | 情形 | 处置 |
|---|---|---|
| -412 | 无 buvid3/WBI 签名错/流量大 | 取/刷新 buvid3 + 8xx 重取 wbi 键;UI 提示"风控拦截,稍后重试" |
| -352 | 部分接口(每周必看/新直播)风控 | 降级到 popular/热门 + 提示 |
| -101 | 未登录但接口需登录 | 引导登录(qr/粘贴) |
| -404 | bvid 无效 | 行内显示"已失效" |
| -509 / -799 | 风控/频次 | 提示"稍后重试" |

## 字段映射 → VideoItem

| VideoItem | Bilibili |
|---|---|
| videoId | `bvid` |
| aid | `aid` |
| title | `title`(paint 标签 "<em class=keyword>" 剥掉) |
| channel / channelId | `owner.name` / `owner.mid`(搜索项 author/mid) |
| durationSec | `duration`(popular 项是秒数字;search 项 "5:30" 需解析) |
| views | `stat.view`(search: `play`) |
| thumbUrl | `pic`(search: 字段 `pic`) |
| cid | 详情 `pages[0].cid` |
| nextPageToken | `"pn=%d" % (pn+1)` |
| live | false(直播归 Phase 5+) |

## 附:参考链接

- BAC(SocialSisterYi/bilibili-API-collect):https://socialsisteryi.github.io/bilibili-API-collect/
- wbi 纯 JS 参考(表与算法): docs/misc/sign/wbi.md
- DeepWiki 数据流参考: https://deepwiki.com/afiuh/bilibili-api-collect/3.2-video-streaming-and-playback
