# qlvotube UI 接入(视频源网格 / 站点 combo / 动态分类)

2026-11-17 落地，覆盖 `qlvotube`（Qt3/Qt4/Qt6 三档构建）。

## 1. 站点 combo（左上角，用户信息下方）

- 位置：`SideNav` 用户信息块与搜索框之间（24px 行，`label + QComboBox`）。
- 来源：`qxv::createProviders()` 的 provider id 列表；显示名走 i18n `site.<id>`
  （`lang/{zh-CN,zh-TW,en-US}.json`），缺译回退 id 原文。
- 交互：只连 `activated(int)`（Qt3/Qt4 都有，程序填充不触发），
  **索引 → provider id 映射**，不做显示文本匹配。
- Qt3/Qt4/Qt6 同码：`providers.pri` 三档均编译真实源（依赖
  `qlappcom/EventPoller` + libcurl）；`createProviders()` 返回 5 源。
  仅当宿主未接入 providers（空表）时整行隐藏。

## 2. 分类 chips 一律动态拉取

`VideoProvider::categories()` → UI chips → `categoryFeed(key, page)`。
UI 层零站点分类硬编码；无能力时退化为单个「推荐」chip（`key=""` → `homeFeed`）。

| 站点 | 分类来源（动态） | 列表接口 | 现状 |
|---|---|---|---|
| bilibili | `popular` 条目自带的 `tid/tname` 去重排序（30min 缓存） | `newlist?rid=&pn=&ps=` 真分页 | ✅ 实测 12 项 |
| 红果 | 首页 SSR `homeSections[].{tab_type,tab_name}` | 同页按 `tab_type` 过滤 | ✅ 实测 5 项（含新增 AI 剧 tab） |
| 抖音 | 免签接口无分类维度（`board_type` 被忽略、`aweme/positive` 空、`board/list` 404） | — | 走缺省 → 仅「热榜」 |
| 小红书 | `/explore` SSR 探测 `channel_list/channels/channel/homefeedChannel` | `/explore?channel_id=` | 风控/未暴露时 → 仅「推荐」 |
| YouTube | 首页 `ytInitialData` 的 `chipCloudChipRenderer`（text + browse params） | 无 continuation 样本 | 空壳页时 → 仅「推荐」 |

## 3. 网格与翻页

`ContentView::VideoGridMode`：

- 标题区 + 分类 chips（吸顶）+ 响应式网格（单元 ≥200px，16:9 封面）。
- 卡片：封面（异步缓存；未命中先 identicon 占位）、标题单行省略、作者 · 播放量。
- 翻页：底部「加载更多」+ 滚动距底 <120px 自动触发（`time(0)` 1s 节流）。
- 失败：错误条（i18n 文案 + 服务端详情）+「重试」，**不清空已加载卡片**。
- 到底：隐藏 footer；空结果：显示「暂无内容」。

## 4. 封面缓存

`videothumbcache`（`EventPoller`/curl）：同一 URL 只请求一次，失败记账不重试；
Referer 按图床域名推导（B站/小红书/YouTube/字节）；代理沿用 `QXVOTUBE_PROXY`。
Qt3/Qt4/Qt6 同一实现（Qt3 用 `QImage::loadFromData` + `QPixmap::convertFromImage`）；
静态对象改为函数内静态，避免 Qt3 在 QApplication 之前构造 QPixmap。

## 5. 日志

统一用 **Qt 原生日志函数**（不引入封装层）：正常流程 `qDebug`，失败 `qWarning`。

版本差异（同一文件内按版本二选一，均为原生调用）：

| Qt | 形态 | 说明 |
|---|---|---|
| 3.3 | `qDebug("fmt %s", qToUtf8(s).data())` | Qt3 只有 printf 形态；`qDebug(QString)` 会截断中文，禁用 |
| 4.8 / 5.x | 同上（printf） | Qt5 的 printf 走 `QMessageLogger`，保留 `__FILE__/__LINE__` |
| 6.x | `qDebug() << "..." << s` | Qt6 删除 printf 重载，只能流式 |

- `QLVOTUBE_DEBUG=1` → debug 行输出（三档 Qt 都生效）；`qWarning` 恒定输出。
- UI 侧日志点：
  - `providers: N sources (ids)` / `site -> <id>` / `chips: N active <key>`
  - `grid: <site> chip <key> page <n>` / `grid: ... -> <m> cards (more)`
  - `grid error: ...`（warning）/ `categories failed: ...`（warning）
  - `thumb: GET/FAILED`（截图缓存）
- providers 库内部分级日志：`QXVOTUBE_DEBUG="bilibili:info,hongguo:info,http:info"`
  （`setupVideoProviders()` 调用 `dbg::loadFromEnv()` 使模块级开关生效）。
  请求 `Info`、成功 `Debug`、空结果/非 2xx `Warn`、结构失败 `Err`。
- 失败文案：`errortext::i18nKeyFor(Result)`（前）+ `Result.message`（后）拼接。

## 6. 验证

```bash
# providers 离线(分类解析用真实 fixture)
cd providers/build && ctest -E providers_smoke

# 动态分类在线(手动;bilibili/youtube 视网络)
QXVOTUBE_PROXY=http://127.0.0.1:10809 ./providers/build/tests/categories

# Qt3 真编 + providers 接入面
qlvotube/buildqt3.sh
providers/tests/qt3stub/build.sh qt3   # providers=0
providers/tests/qt3stub/build.sh qt6   # providers=5

# Qt6 UI
qlvotube/buildqt6.sh
QLVOTUBE_DEBUG=1 QXVOTUBE_PROXY=http://127.0.0.1:10809 xvfb-run -a ./qlvotube/build-qt6/q6votube
```

## 7. 已知限制

- Qt3/Qt4 不启用视频源 UI（providers 仅 Qt>=5 实现）。
- 抖音分类榜需 `a_bogus/Argus` 签名（见 `protocols/bytedance.md` §13）。
- 小红书频道字段名需登录态真实 SSR 才能确认；当前探测不到即不显示。
- YouTube 分类需可达代理（当前出口为空壳页），翻页还需 continuation 样本。

## 8. 2026-11-17 补充修复（Qt3 实测暴露）

- **协议相对封面**：B站搜索接口 `pic` 形如 `//i0.hdslb.com/...`。
  provider 侧补 `https:`（`bili_parse.cpp::picUrl`），封面缓存再做一层 `normUrl` 防御。
- **搜索标题高亮**：B站 `title` 含 `<em class="keyword">`，provider 侧 `stripTags` 剥除。
- **搜索回车**：`SearchLineEdit` 之前只在有联想项时触发；现无联想项时用输入原文触发
  （qlcomp 共享修复，qt3 实测 `search submitted: music`）。
- **触底翻页**：滚轮滚动不经过 scrollbar 的 `valueChanged`，新增 `maybeLoadMore()`
  在 `wheelEvent/onScroll` 共同调用（Qt3 实测 `near bottom -> loadMore` → p2 20 条）。
- **Qt3 静态 QPixmap**：`QMap<QString,QPixmap>` 改为函数内静态，否则
  Qt3 在 QApplication 创建前构造 QPixmap 直接 `qFatal`。

## 9. 播放直链网络选项（2026-11-17 追加）

B站等站点的媒体直链绑定「请求出口 IP + Referer/UA」，缺一即 403。
本轮把选项打通到播放引擎：

- `cxengine.h`：新增 `CxNetOptions{proxy, referer, userAgent}` 与
  `CXEngine::setNetOptions()`（默认空实现，不影响既有宿主）。
- `eng_ffmpeg`：`av_dict_set("headers", "Referer/User-Agent", …)` +
  `av_dict_set("http_proxy", …)` 后 `avformat_open_input`。
- `eng_mpv`：`loadfile` 前设置 `http-proxy` / `http-header-fields`（含 Quick 二段起播）。
- `CXVideoPlayer::setNetOptions()`：`setEngine` 之后自动透传。
- providers 补元数据：bilibili/红果/YouTube/小红书 的 `Stream.extra` 提供
  `referer` / `userAgent`；UI 在播放前组装 `CxNetOptions`，
  未显式指定 proxy 时沿用 `QXVOTUBE_PROXY`（与 API 同出口）。

Qt3 实测：`pos=0→29000ms` 连续推进，无 `HTTP error 403`；Qt4 同样通过。
