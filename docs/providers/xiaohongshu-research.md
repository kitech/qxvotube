# 小红书（xiaohongshu）免登录可行性调研

> 调研日期：2026-11-15　环境：本机直连（无代理）　方法：curl 实测 + 源码级核对
> 结论一句话：**游客可浏览（SSR），但搜索必须登录；详情需要配对 token。**

## 1. 能力矩阵（实测）

| 能力 | 免登录 | 证据 |
|---|---|---|
| 首页 `/explore` SSR | ✅ | 200 / 172–185KB；`__INITIAL_STATE__.feed.feeds` 27–28 条，含 `xsecToken` |
| 首页频道（翻页替代） | ✅ | `channel_id=homefeed_recommend/fashion_v3/food_v3/travel_v3` → 26/23/18/24 条 |
| 笔记详情 `/explore/<id>?xsec_token=…` | ✅（**必须带 token**） | 有 token → `noteDetailMap[id]` 含 `video.media.stream.h264[].masterUrl` + `backupUrls`；无 token → detail 恒空（页面仍 200） |
| 用户主页 `/user/profile/<uid>` | ✅（建议先预热 cookie） | 先访问 `/explore` 后 256KB / 30 条 noteCard；冷启动直连只有 37KB 壳 |
| 笔记评论 | ⚠️ 仅 SSR 首屏，样例为 null | 详情 entry 有 `comments` 字段；不额外发 API 请求 |
| 搜索 `/search_result` | ❌ | SSR `search.feeds` 恒空（Chrome/Googlebot/Baiduspider/bingbot 全试）；`search.state=auto` 说明结果由前端 XHR 拉取 |
| Web API（search/feed/homefeed/comment） | ❌ | 带**正确 x-s 签名**仍 `{"code":-101,"msg":"无登录信息"}` |
| 旧版/移动接口 | ❌ | `/api/sns/v1/search/notes` → 500 `jarvis-gateway`；`m.xiaohongshu.com/api/sns/v1/...` → 301；`search/querytrending` → 406；`search/suggest` → 404 |

## 2. 关键机制

### 2.1 `xsec_token`（决定详情能否打开）
- 按**笔记**签发，形如 `ABA4qmwb...=`（44 字符 base64）
- 来源只有三处：
  1. 首页 feed（`feed.feeds[].xsecToken`）
  2. 用户页（`user.notes[].noteCard` 与 `userNoteQueries`，同页约 60 个）
  3. **分享链接**（URL 自带 `?xsec_token=…`）
- 缺失即详情为空 → 不能"猜"或"复用"（token 与 noteId 绑定）

### 2.2 SSR 数据位置
- `window.__INITIAL_STATE__ = {...}`（非 `_ROUTER_DATA`）
- 解析注意：JS 里含 `undefined`，需替换为 `null` 后再解析；`</script>` 截断
- 详情页 `feed.feeds` 为空（相关推荐由 XHR 拉取，游客拿不到）

### 2.3 视频流
- `note.video.media.stream.{h264,h265,av1,h266}[]`
- 单档字段：`masterUrl`（**http://**，需改 https）、`backupUrls[1]`、`width/height/fps/size/avgBitrate/audioCodec`
- 图文笔记无 `video` 字段 → 无视频流

### 2.4 API 需要登录（不是签名问题）
- 用 `Cloxl/xhshow`（纯算法 x-s，可离线复现）实测：
  - `search/notes`、`feed`、`homefeed` 全部 `-101 无登录信息`
  - 说明服务端判的是 `web_session`（登录态），签名正确也无效
- MediaCrawler issue #757：461/406 属**风控态**，即使签名正确；换账号/cookie 未必恢复（含 IP 维度）

## 3. 风控与频率（源码级依据，官方无公开阈值）

### 3.1 主流开源实现的默认节流
**XHS-Downloader（master）** `source/module/tools.py` + `settings.py`：

```python
avg_delay = 6.0; sigma = 0.6
mu = log(avg_delay) - sigma**2/2
delay = lognormvariate(mu, sigma)
clamp(delay, avg_delay*0.2, avg_delay*5)   # [1.2s, 30s]
# 每个请求后都 sleep；max_retry = 5；timeout = 10s
```

→ 中位数 ≈5s，约 68% 落在 2.7–9s。

### 3.2 其它资料（区间跨度大，说明无统一阈值）
| 来源 | 口径 |
|---|---|
| juejin 采集工具（2026） | 默认 2s，"建议 ≥1s" |
| socialext 安全采集建议 | 30–60s/次，避免长时间连续 |
| CSDN 风险文档 | >20 次/分钟 判"请求频率异常"；间隔 <3s 风险高 |
| XHS-Downloader #288 | 触发后约 24h 自动恢复（评论口径） |
| XHS-Downloader README | 旧作品链接（含日期信息）也可能被风控 → 链接/token 要新鲜 |
| wukongdaili 防封指南 | 机房 IP 权重低，同 IP 短时大量请求触发频率限制 |

### 3.3 本机实测现象
- 约 2 分钟内 ~30 次请求（≈15/min）后，详情页降级为 37KB 壳（`__INITIAL_STATE__` 在但 `noteDetailMap` 空）
- 换 UA（Googlebot/Baiduspider/bingbot）无效；带 cookie 预热可恢复

## 4. 设计结论（provider 用）

| 项 | 取值 | 依据 |
|---|---|---|
| 请求间隔 | 均值 6s，对数正态 σ=0.6，clamp 1.2–30s | XHS-Downloader 源码默认 |
| 每分钟上限 | 10 次 | 6s 均值推导，且低于 20/min 风险线 |
| 日上限 | 100 次内容抓取 | XHS-Downloader 风险提示 |
| token 缓存 TTL | 5 分钟 | README：旧链接会被风控 |
| 风控冷却 | 5 → 30 → 120 分钟指数升级 | 无公开值；24h 属换 IP 级 |
| 重试 | ≤5 次，带退避 | XHS-Downloader max_retry=5 |
| 全部参数 | env 可覆盖 | `QXVOTUBE_XHS_*` |

## 5. 可实现范围

- ✅ `homeFeed(page)`：explore + 频道轮转
- ✅ `detail/resolve`：`noteId@xsecToken` 或 token 缓存/分享链接
- ✅ `userVideos(uid)`：预热 cookie 后解析 `user.notes`
- ✅ `comments`：仅 SSR 首屏（有则给）
- ❌ `search`：`Err::NeedAuth`（无免登录路径）
- 备注：**零用户输入**（无 cookie/登录/签名/设备），唯一例外是从外部拿到的裸 noteId 无 token

## 6. 风险

1. SSR 结构改版（`__INITIAL_STATE__` 字段名）→ 递归遍历 + fixture 回归
2. `xsec_token` 短时效 + 旧链接风控 → 缓存 5 分钟、失败即刷新 feed
3. 反爬降级不可预测 → 冷却 + 限流 + 仅在必要时请求
4. 机房 IP 权重低 → 文档建议住宅代理

## 7. 引用

| 资料 | 用途 |
|---|---|
| XHS-Downloader（master）`source/module/tools.py`、`settings.py` | 默认节流/重试（均值 6s 对数正态） |
| XHS-Downloader issues #224 / #288 / #416 | 间隔建议、24h 恢复、默认延迟机制 |
| MediaCrawler issue #757 | 461/406 风控态；签名正确也可能触发 |
| MediaCrawler `config/xhs_config.py` | 详情 URL 必须带 `xsec_token`（佐证） |
| Cloxl/xhshow | 纯算法 x-s（用于证明"API 失败与签名无关"） |
| wukongdaili 防封指南 | IP 信誉权重 |
