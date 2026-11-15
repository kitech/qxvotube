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
