# 小红书（xiaohongshu）协议口径

> 调研全文见 `docs/providers/xiaohongshu-research.md`。本文件只记实现口径。

## 能力矩阵

| 能力 | 实现 | 说明 |
|---|---|---|
| `homeFeed(page)` | ✅ | page1=`/explore`；page≥2=频道轮转（`homefeed_recommend/fashion_v3/food_v3/travel_v3/career_v3/movie_and_tv_v3`） |
| `detail(id)` | ✅ | 必须 token：`noteId@xsecToken` / 分享链接 / token 缓存（来自 homeFeed/userVideos） |
| `resolve(id)` | ✅ | 视频笔记 → `video.media.stream.{h264,h265,av1}[].masterUrl`（http→https）+ `backupUrls` |
| `userVideos(uid)` | ⚠️ | `/user/profile/<uid>` SSR；实测 id 常为空（登录才带）→ 条目标 `playable=false`；本 IP 还可能被软风控 |
| `comments` | ❌ | 仅详情 SSR 首屏，未单独提供 |
| `search` | ❌ | `Err::NeedAuth`（SSR 空 + API `-101`，签名正确也无效） |

## 关键结构

- SSR 变量：`window.__INITIAL_STATE__`（注意不是 `_ROUTER_DATA`；含裸 `undefined`，需替换为 `null`）
- feed：`feed.feeds[] = {id, xsecToken, noteCard:{type,displayTitle,user,cover,interactInfo}}`
- 详情：`note.noteDetailMap[<id>].note`
- 用户页：`user.notes` = **tab 数组的数组**，条目 `{id?, noteCard, xsecToken}`（id 常为空）
- 视频：`note.video.media.stream.h264[0]` 字段 `masterUrl/backupUrls/width/height/fps/size/avgBitrate/audioCodec`

## 节流与风控（源码级默认）

| 参数 | 默认 | env |
|---|---|---|
| 请求间隔 | 均值 6s，对数正态 σ=0.6，clamp 1.2–30s | `QXVOTUBE_XHS_DELAY_MS` |
| 每分钟上限 | 10 | `QXVOTUBE_XHS_MAX_PER_MIN` |
| 每日上限 | 100 | `QXVOTUBE_XHS_MAX_PER_DAY` |
| 风控冷却 | 5→30→120 分钟指数升级 | `QXVOTUBE_XHS_COOLDOWN_MS` |
| token 缓存 | 5 分钟 | 常量 |

风控信号：HTTP 461/406、302→/login、`__INITIAL_STATE__` 存在但 `feeds/noteDetailMap` 为空 → `Err::RateLimit`（带 `retryAfterMs`）或 `Err::CaptchaRequired`。

## 测试

- 离线：`ctest -R providers_parse`（fixture：explore/detail/user + SSR undefined 容错 + 分享链接解析）
- 联网：`./build/tests/xhs`（homeFeed→resolve→流头 1MB；search 期望 NeedAuth；userVideos 软风控时 skip）

## 认证模式与错误

| 模式 | 判定 | 典型错误 |
|---|---|---|
| guest(默认) | 未设置任何凭证 | `NeedAuth` + `auth.mode=none` |
| cookie | `setCredentials({"cookie":…})` | 失效 → `AuthExpired` + `server.code=-101` |
| oauth(预留) | `{"token":…}` | `AuthExpired` + `error.auth_oauth_expired` |
| password(预留) | `{"username","password"}` | `NeedAuth` + `error.auth_password_failed` |

服务端原始信息一律进 `http.status/server.code/server.message`（脱敏）；
UI 用 `i18nKeyFor(Result)` 选主文案，再附加服务器消息。

## 登录态签名器(sign.h/cpp, 2026-11-17)

xhshow(Cloxl/xhshow)移植:**纯 C++、零 Qt 依赖**(std::string + stdint),
故 Qt3/Qt4/Qt5/Qt6 任意宿主直接可用;Qt3 的 `QByteArray` 残缺 API 完全绕开。

| 件 | 来源 |
|---|---|
| MD5 | qlcomp `md5.{h,c}` |
| RC4 | `vendor/wjcryptlib`(公有领域) |
| CRC32 | 系统 zlib `crc32()`;无 zlib 平台 `QXV_NO_ZLIB` 走 `vendor/zlib-crc32` |
| base64 | sign.cpp 内置标准编码 + 两张替换表(主表 / x3 表) |

产出:
- `x-s` = `XYS_` + 主表B64(`{"x0":"4.3.5","x1":"xhs-pc-web","x2":"Windows","x3":"mns0301_"+X3B64(payload),"x4":"object"}`)
- `x-s-common` = 主表B64(模板 + `x5=a1`、`x8=b1`、`x9=crc32(b1)^0xEDB88320`(有符号))
- payload 144 字节:版本/seed/ts/effective_ts/seq/window_props/uri_len/md5^seed/a1/appid/env 校验/tail(mns0301)

关键等价(差分证实):
- b1 的 URL-quote 展开 ≡ **latin1→UTF-8 展开**(Python `quote()` 默认按 UTF-8 重编码)
- x9 = `zlib_crc32(b1) ^ 0xEDB88320`(`-1 ^ c ^ POLY` 的中间态等价式)

验证(全离线):
- `ctest -R providers_xhssign`:KAT(MD5/RC4/CRC32)+ **1000 组 Python xhshow 差分对拍** + 结构自证
- `providers/tests/qt3sign/build.sh`:`/opt/qt338sh` 下同一份源码再跑一遍,全绿

## 登录态接入(2026-11-17)

- 凭证:`setCredentials({"cookie": "a1=…; web_session=…"})`;缺 `web_session` 或
  `a1` → 登录态不可用(`NeedAuth`,detail 带 `auth.mode=cookie, provided=true`)
- 签名头:`x-s` / `x-s-common` 由 `sign.{h,cpp}` 生成(纯 C++,Qt3 亦可用)
- 探测:`GET /api/sns/web/v1/user/selfinfo`,成功缓存 5 min;`-101`/401 →
  `AuthExpired`(detail: `auth.expired=true`, `server.code=-101`)
- 风控:`461/406/429` → `RateLimit` + 指数冷却(`retryAfterMs`)
- **dry-run**:`QXVOTUBE_XHS_DRYRUN=1` 时打印签名头长度/前缀(不落完整签名),
  用于确认"签名器在真实 cookie 上工作"
- 边界:游客一律 `-101`,无 cookie 环境取不到响应 → 解析器按 schema 实现,
  用**合成 fixture** 离线验证;取得 cookie 后跑一次即完成真实样本验证(见下)

## 登录态 web API(2026-11-17,schema 派生)

| 能力 | 端点 | 参数 |
|---|---|---|
| 搜索 | POST `/api/sns/web/v1/search/notes` | keyword/page/page_size/search_id/sort/note_type |
| 详情 | POST `/api/sns/web/v1/feed` | source_note_id/image_formats/extra/xsec_source/xsec_token |
| 评论 | GET `/api/sns/web/v2/comment/page` | note_id/cursor/top_comment_id/image_formats/xsec_token |
| 用户笔记 | GET `/api/sns/web/v1/user_posted` | num/cursor/user_id/image_formats/xsec_token/xsec_source |
| 探测 | GET `/api/sns/web/v1/user/selfinfo` | — |

请求头:`x-s` / `x-s-common` / `x-t`(毫秒时间戳) / `x-b3-traceid`(16 hex) / `x-mns: unload`。

GET 签名口径:`content = uri + "?" + queryEncode(params)`,**逗号不转义**(safe=",")。

响应字段(MediaCrawler `client.py`/`core.py` 源码级):
- 搜索 `data.items[]`:`{id, model_type, xsec_token, xsec_source, note_card}`;
  `model_type ∈ {rec_query, hot_query}` 为聚合词条,必须跳过;`note_card` 内
  `display_title/type/user{user_id,nickname}/cover{url_default}/interact_info{liked_count}`
- 详情 `data.items[0].note_card`;播放流 `video.media.stream.h264[].master_url`
  (h265 兜底,`backup_urls` 备选),时长 `video.capa.duration`
- 评论 `data.comments[] / cursor / has_more`;条目 `id/content/user_info.nickname/like_count/create_time`
- 用户笔记 `data.notes[] / cursor / has_more`

**验证程度**
- 请求参数/端点/头:源码级确定(MediaCrawler + xhshow 一致)
- 响应解析:按 schema 手写合成 fixture,离线单测 `providers_xhsapi`(ctest)
- 未过真实服务端(游客 -101 无法区分"签名错"与"未登录")

### 能力路由(登录态与否)

| 能力 | 游客 | 登录态 |
|---|---|---|
| homeFeed | SSR explore | SSR explore(不变) |
| detail | SSR(需 xsec_token) | SSR → 失败时 API `/feed` 兜底(仅 VideoItem) |
| resolve | SSR 解析流 | SSR → 失败时 API `/feed` 兜底(`stream.h264`,quality 按分辨率标签化) |
| search | `NeedAuth` | API `/search/notes`(过滤 rec_query/hot_query) |
| comments | `Unsupported` | API `/v2/comment/page`(cursor 分页) |
| userVideos | SSR 首屏(30 条,无分页) | API `/user_posted`(cursor 分页) |

> API 兜底只在 `hasSession()` 时启用,SSR 的错误语义保持原样(仅多一次机会);
> 两条链路都失败时返回原 SSR 错误。

**真实样本补齐(一条命令)**
```bash
export QXVOTUBE_XHS_CAPTURE=/tmp/xhs_cap   # apiRequest 落盘每个响应
# 设置 cookie 后跑一次 pvsmoke/搜索;把 /tmp/xhs_cap/*.json 挑代表样本
# 复制为 providers/tests/fixtures/xhs_api/*.json 覆盖同名文件即可
```
