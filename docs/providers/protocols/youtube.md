# YouTube 协议文档(InnerTube 游客口径)

来源:**yt-dlp** / **pytubefix** 的 InnerTube client 档位 + **iv-org/invidious**
的风控研究。全部 POST `https://www.youtube.com/youtubei/v1/{endpoint}?prettyPrint=false`。

## 接口链

| 步骤 | 端点 | body 摘要 |
|---|---|---|
| 预热 | `GET https://www.youtube.com/`(Chrome UA) | 种 YOS/SOCS 等 cookie;**必须**,裸 UA 会拿到风控页种毒 cookie |
| 搜索 | `POST /search` | client=WEB;`query`;响应里任意层级递归收 `videoRenderer`(videoId/title.runs[0]/ownerText/thumbnails 末档) |
| 播放 | `POST /player` | client=**ANDROID_VR**;`videoId`;`streamingData.formats[]`(progressive,免 decipher) |

## ANDROID_VR 档(2026 实测口径)

```
clientName=ANDROID_VR clientVersion=1.60.19
deviceMake=Oculus deviceModel="Quest 3"
osName=Android osVersion=12   hl=en gl=US
UA: com.google.android.apps.youtube.vr.oculus/1.60.19 (Linux; U; Android 12;
    eureka-user Build/SQ3A.220605.009.A1) gzip
```

- 免 API key、免 PoToken、免 X-Youtube-Client-* 头;**直出已签名 url**(免 decipher)。
- 干净/住宅 IP 上实测 OK:progressive 1 + adaptive 26。
- 备选 IOS 档:`19.45.4`,`com.google.ios.youtube/... (iPhone16,2; ...)`。

## 网络(必须代理)

- YouTube 被墙,直连必超时。`setProxy()` 或 `QXVOTUBE_PROXY=http://host:port`。
- 代理慢:验证只拉**流开头 ≤1MB**(`Range: bytes=0-1048575`)。

## 风控映射(playabilityStatus.status)

| status/reason | Err |
|---|---|
| OK | Ok |
| LOGIN_REQUIRED + "not a bot"/captcha/unusual traffic | **CaptchaRequired**(detail.captchaUrl) |
| LOGIN_REQUIRED(年龄) | NeedAuth |
| UNPLAYABLE(地区) | GeoBlocked |

## IP 级 bot 风控(实测结论,2026-11)

> **2026-11-15 更新**:本节结论(必须 PO token)已被下方"ANDROID 客户端免 token"推翻,
> 保留作为历史记录;当前实现见文末定案章节。

`LOGIN_REQUIRED: Sign in to confirm you're not a bot` 是 **IP 级**封锁:
数据中心出口 IP 全员命中(含 ANDROID_VR+visitorData、TVHTML5、带 cookie 全试过);
与实现无关(curl 复测同样现象)。干净 IP 上 ANDROID_VR 裸通。

### 解法路线(P2)

1. **BGutil PO token**(yt-dlp 插件 `BGutil-youtube-pot-provider`):
   BotGuard attestation 生成 PO token,ANDROID_VR 携带即可解 datacenter IP。
   (ytagent-cli 在 Alibaba HK datacenter IP 实测通过,见其 proof-of-work)
2. WEB client + po_token/visitor_data(invidious PR#4789 口径,
   由 youtube-trusted-session-generator 生成;PO token 仅对 WEB 有效)。
3. 住宅代理(转嫁出口 IP,最省事但依赖代理质量)。
4. SABR 迁移注意:invidious #5263,YouTube 正推 SABR 流格式,P2 需跟踪。

## 引用来源

| 资料 | 链接 |
|---|---|
| invidious 主库 | https://github.com/iv-org/invidious |
| invidious #4734(bot 检查全局事件) | https://github.com/iv-org/invidious/issues/4734 |
| invidious PR#4789(po_token/visitorData 配置) | https://github.com/iv-org/invidious/pull/4789 |
| 官方错误方案文档 | https://docs.invidious.io/youtube-errors-explained/ |
| trusted-session-generator | https://github.com/iv-org/youtube-trusted-session-generator |
| BGutil POT provider | https://github.com/Brainicism/bgutil-ytdlp-pot-provider |
| yt-innertube(ANDROID_VR 参考) | https://github.com/AlvaroBalbin/yt-innertube |
| SABR 讨论 | https://github.com/iv-org/invidious/issues/5263 |

## 2026-11-15 实测定案:ANDROID 客户端免 PO token 直接可播 ✅

此前"必须 PO token"的结论**被推翻**。该出口 IP(经代理)实测矩阵:

| 客户端 | playability | formats(progressive) | adaptive | 结论 |
|---|---|---|---|---|
| **ANDROID 21.26.364**(sdk30) | **OK** | **1 条 itag18 360p mp4(h264+aac)直链** | 35(无 url) | ✅ 采用 |
| ANDROID_VR 1.60.19 / 1.65.10 | LOGIN_REQUIRED "not a bot" | — | — | ❌ IP 级拦截 |
| IOS 21.26.4 / VISIONOS 1.02 | OK | 0 | 32(无 url) | ⚠️ 仅 SABR,无直链 |
| WEB(safari/TVHTML5/tv_samsung/MWEB) | UNPLAYABLE "page needs to be reloaded" | — | — | ❌ 需 ytcfg/其余校验 |
| ANDROID 搜索 | OK | `compactVideoRenderer` 19 条 | — | ✅ 采用 |

ffprobe 校验 itag18 直链:`h264 640x360 + aac, duration 1953s` → 真可播。

### 关键坑位(全部实测)
1. **客户端与 UA 必须同源**:`searchBody` 写 WEB 但 UA 用 Android → 恒空结果;
   两端都用 kDefaultClient(ANDROID)。
2. **cookie 与访客身份互斥**:预热种的 cookie + `X-Goog-Visitor-Id` 同时带 →
   search 恒 `backgroundPromoRenderer` 空结果;`autoCookie=false` 后正常。
3. **首页 ytcfg 的 visitorData 不等于可用访客身份**;POT 的 `contentBinding` 有时可用、
   有时不行(不稳定),不能作为唯一依赖。
4. **上游偶发 `HTTP 200 + 232B {"error":{"code":500,backendError}}`**:
   必须重试(provider 内最多 3 次),否则表现为"搜索 0 条"。
5. 空结果不再静默 `Ok+0 条` → `Err::RateLimit`(message 带重试次数)。
6. 直链会 302 到 `rr*.googlevideo.com`:httplib 必须 `set_follow_location(true)`
   (NetClient 已开),Range 生效返回 206。

### BGutil PO token 集成(保留为可选路径)
- 真实 API:`POST {base}/get_pot` body `{"proxy":"http://.."}` →
  `{"poToken","contentBinding","expiresAt"}`;`contentBinding` 即绑定访客身份,
  需原样放进 `context.client.visitorData` 与 `X-Goog-Visitor-Id` 头。
- 本机验证方式(无 docker):
  ```
  git clone(gh-proxy) https://github.com/Brainicism/bgutil-ytdlp-pot-provider  # master 分支
  cd server && npm ci         # 需代理:canvas 要下 prebuild
  npx tsc && node build/main.js -p 4416
  curl -X POST 127.0.0.1:4416/get_pot -H 'Content-Type: application/json' \
       -d '{"proxy":"http://127.0.0.1:10809"}'      # ≈25s,返回 poToken+contentBinding
  ```
- provider 侧:`QXVOTUBE_YT_POTURL=http://127.0.0.1:4416` 才启用(默认不探测本地口,
  避免每次搜索等 25s);`QXVOTUBE_YT_POTOKEN`/`QXVOTUBE_YT_VISITORDATA` 为静态注入。
- **实测局限**:该 IP 上 WEB+PO token 仍拿不到直链(YouTube 只给 SABR adaptive),
  所以 PO token 在当前阶段不是可播路径;ANDROID 裸通才是。保留代码以便:
  ① 换住宅 IP 后 WEB 可用 ② 后续做 DASH 合并时用 adaptive 直链。

## 搜索分页(2026-11-15 实测)

- ANDROID 客户端搜索响应的续页游标有两种历史口径,provider 均解析:
  - 新式:`continuationCommand.token`
  - 旧式:`nextContinuationData.continuation` / `reloadContinuationData.continuation`
  (实测 "music" 走旧式)
- 续页请求:`POST /youtubei/v1/search` body `{"context":{client},"continuation":"<游标>"}`。
- **游标链只能按序取**:第 N 页游标由第 N-1 页响应给出;provider 内
  `m_searchTokens[kw][page]` 缓存,未命中显式 `Err::Parse`(不静默回空);
  缓存上限 16 个关键词。
- 部分查询无续页(实测 "travel" hasMore=false),属正常。
- 验收:`tests/paging` → page1 20 条 / page2 17 条 / 跨页重复 0。

## 清晰度(resolveQuality,2026-11-15)

- 当前仅 itag18(progressive 360p,h264+aac);请求更高档 → 回落 360p,
  `Stream.extra.degraded=true` + `requestedQuality`,不伪报清晰度。
- 1080p 需 DASH adaptive video+audio 合流(Wave D)+ localstream。
- 验收:`tests/quality` → `1080p` 得 `640x360 degraded=1`。

## 清晰度词汇(2026-11-15 补充)

- 统一词汇 `144/160/240/360/480/540/720/1080`;当前所有请求都落 itag18(360p),
  因此除 360p 外一律 `degraded=true`(含请求 160/240 这种"更低"的档)。
- `Stream.quality` 为真实分辨率 `WxH`;`extra.actualQuality` 同值。
- 若上游 OK 但 formats 为空(SABR 抖动)→ 自动重试一次,仍空才 `Err::Parse`。

## homeFeed(2026-11-15 实测:不可用)

- ANDROID `browse`:`FEwhat_to_watch` 返回 1MB 结构壳(0 个 video renderer);
  `FEtrending`/`FEexplore` 直接 285B 错误(该 browseId 不支持)。
- WEB `browse` 同空壳;`/feed/trending` 页面 SSR 也被风控。
- 结论:provider 返回 `Err::Unsupported` 并提示改用 `search`,
  **不返回假数据**;若后续换住宅 IP / 登录态可再评估。

## detail(2026-11-15)

- 单视频:`videoDetails` → title/author/channelId/lengthSeconds,
  `extra` 带 `viewCount`/`shortDescription`。
- **章节不支持**:ANDROID player 响应无 `chapterRenderer`/`macroMarkers`
  (章节是 WEB 渲染器功能),实测 3 个视频均为 0。
- **播放列表**:id 以 `PL/UU/OL/FL/RD` 开头且长度 >11 → `browse` `VL<id>`,
  用 `watchEndpoint{videoId,playlistId}` 收集分集(ANDROID 无 `playlistVideoRenderer`)。
  局限:browse 首屏只有 **10 条**(续页需 continuation,未实现);
  条目标题不在该渲染层,`part.title` 留空(UI 可按需逐条 detail)。
- 验收:`tests/detail` → 单视频 title 非空;官方示例 PL → 10 条。

## 高清晰度:yt-dlp + HLS(2026-11-15 实测)

### 为什么不是"纯客户端 DASH 直链"
- TVHTML5 / WEB 全变体在本机出口 IP 均 `LOGIN_REQUIRED`/`UNPLAYABLE`;
  ANDROID/IOS 的 `adaptiveFormats` **无 url(SABR)**。
- 但 `yt-dlp -J` 能拿到完整 HLS 主播放列表(`manifest_url`,hls_variant),
  其中 `#EXT-X-MEDIA`(音频)+ `#EXT-X-STREAM-INF` 到 2160p,且 variant 自带音频。

### 实现
1. `common/ytdlp.{h,cpp}`:异步 `yt-dlp -J --proxy ... --skip-download` → formats +
   `hlsMaster`;`QXVOTUBE_YT_YTDLP=0/1` 可强制开关,默认探测 PATH。
2. YouTube `resolveQuality`:`quality>360p` 且 yt-dlp 可用 → **HLS 路径**;
   否则回落 itag18。
3. HLS 路径:`GET hlsMaster`(经代理)→ `trimHlsMaster()` 按 `RESOLUTION`
   裁剪到 ≤ 请求高度(保留 `EXT-X-MEDIA` 音频)→ `localstream::BufferSource`
   以 `application/vnd.apple.mpegurl` 提供 → 本地 `kind=hls` URL。
4. `Stream.extra`:`hlsMaster=true`、`proxy`(分片在 googlevideo,播放器需代理)、
   `userAgent`、`requestedQuality`。

### 验收(tests/ytdash,需外网+代理+yt-dlp+ffmpeg)
```
[ ok ] kind=hls / 本地 URL / extra.hlsMaster
[ ok ] ffprobe 1080p        (1920,1080)
[ ok ] ffmpeg 解码 30 帧
```
`tests/quality`:youtube 1080p → `1080P degraded=0`(≤360p 仍走 itag18 标降级)。

### 局限
- **播放器必须自备代理**(分片直连 googlevideo);`Stream.extra.proxy` 已给出。
- HLS master 只能按"≤请求高度"裁剪,不能精确锁单档(多码率自适应由播放器决定)。
- 依赖 yt-dlp 的外部二进制(可选,缺失自动回落 360p)。
