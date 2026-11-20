# Netflix 协议文档(目录展示 + 无源报错)

## 定位

Netflix 无官方公开 API;正片需订阅登录且受 Widevine/PlayReady/FairPlay DRM 保护,
拿不到可播放直链。本 provider **只做目录展示**(分类 + 列表 + 详情),不提供播放流:
`resolve()` / `resolveQuality()` 固定返回 `Err::Unsupported`(UI 复用
`error.unsupported`:暂不支持该平台)。

公开 trailer 托管在 YouTube/Tudum,不属于 Netflix 站内流,不在本 provider 范围内。

## 数据来源:两者结合

1. **在线**:uNoGS(unogsng, RapidAPI),每日抓取 30+ 国家目录。
2. **本地数据集**:编译进二进制的 400 条真实标题集(`netflix_data.cpp`,来自公开
   netflix_titles.csv)。

策略:**分类 / 列表 / 详情都是「实时拉取 + 本地合并」组合**。
- 始终发 uNoGS 请求(有 key 用 key,无 key 发空 key → 401)。
- 在线结果与本地数据按 id 去重组合展示(列表按页推进窗口,每页 20 条)。
- 在线失败只记日志(`softFail`:Err 记录进 `detail`、message 进日志,对外返回 Ok),
  **本地那半边照常显示**——UI 在 `result != Ok` 时会丢弃整个 items,故不能回 fail。
本地集无封面 URL(UI 显示 identicon 占位),在线集有 Netflix CDN 海报。

## 认证:key 可选(重要)

- **不需要** Netflix 账号/订阅登录(uNoGS 的价值就是免订阅查目录)。
- **RapidAPI key 可选**,通过 `QXVOTUBE_UNOGS_KEY` 提供(不硬编码、不设登录 UI)。
  - 有 key → 正常拉取,在线 + 本地合并展示;
  - 无 key → 仍发请求(空 key 头),API 返回 401 + `Invalid API key...`,日志报错,
    同时照常显示本地分类 / 本地 400 条列表(可滚动翻页);
  - 未订阅 403 / 限频 429 / 网络失败 → 同样日志报错,本地数据照常显示。
- 获取:RapidAPI 注册账号 → 订阅 `unogsng` BASIC 免费层 → 复制 key。

## 接口链(uNoGS,仅元数据)

| 用途 | 端点 | 参数 |
|---|---|---|
| 分类 | `GET /genres` | 无 → `results[].genre` |
| 列表 | `GET /search` | `limit`/`offset`/`genre`/`orderby` |
| 搜索 | `GET /search` | `query`/`limit`/`offset` |
| 详情 | `GET /title` | `id` → `results[0]` |

请求头:
```
x-rapidapi-key:  $QXVOTUBE_UNOGS_KEY
x-rapidapi-host: unogsng.p.rapidapi.com
```
代理:netflix 在 `siteMustProxy()` 清单内,遵循 `QXVOTUBE_PROXY` / 设置页代理。

## 分类时序:本地秒显 + 在线后台增量合并(方案 A)

分类走「本地优先 + 后台增量」,避免本地分类被在线 `/genres` 的 401 往返拖住
(旧实现把本地与在线放同一回调,必须等在线回来才一次性显示,切换站点要等几秒):

1. `categories()` **同步立即**回本地 41 项(推荐 + 40 genre)→ UI 秒显分类并
   正常加载推荐页,完全不碰网络。
2. `categoriesRefresh()` 后台拉 `/genres`:成功 → **在线优先去重**合并(首项恒为
   「推荐」、在线 genre 在前、本地仅补在线没有的),通过独立的 `CategoriesRefresh`
   UI 事件就地刷新 chips;失败(含 401)→ `softFail` 记日志、不回调,保持已显示的
   本地分类不变。
3. 就地刷新只更新 chips 内容 + 侧栏高亮(用当前 `m_gridChip` 的 key 重定位,
   找不到则保持原选中),**不重置分页、不重载页面、不打断用户当前选中的分类**。
   该事件跳过 `gen` 代际校验(首次 `Categories` 分支的 `loadVideoPage` 会递增
   `m_gridGen`),改用站点 id 校验防止切源后旧事件污染。

`VideoProvider::categoriesRefresh()` 默认空实现,其它 provider 不受影响。

## 映射规则

- `genres` → `CategoryItem`(空 key = 「推荐」走 homeFeed);本地路径从
  数据集 genre 字段派生。
- `results[]` → `VideoItem`:`nfid→id`、`title→title`、`boxart→cover`、
  `year→publishMs`、直接拼 `webUrl = netflix.com/title/<id>`。
- 分页:`limit=20`、`offset=(page-1)*20`;`hasMore = 在线本页满 20 条 ||
  本地补充窗口未到尾`。在线翻页结果在前,本地补充按 `(page-1)*20` 推进
  (每页最多 20 条、按 id 去重),两者不互相错位。

## Err 映射(实测 RapidAPI 网关错误码)

| 场景 | 实测 HTTP | 响应 `message` | Err |
|---|---|---|---|
| 无 key / key 无效 | 401 | `Invalid API key. Go to https://docs.rapidapi.com/docs/keys-and-key-rotation for more info.` | `AuthExpired` |
| 有 key 但未订阅 | 403 | `You are not subscribed to this API.` | `AuthExpired` |
| 请求过多 | 429 | `Too many requests` | `RateLimit` |
| 其它非 2xx | 4xx/5xx | — | `Http` |
| curl 网络错误 | — | — | `Network` |
| 解析失败 / 解析空 | 200 | — | `Parse` |
| `detail` 未找到该条目 | 200 | `results[]` 空 | `NotFound` |
| 播放(resolve/resolveQuality) | — | — | `Unsupported` |

失败时 `message` 带 HTTP 码与服务端 `message` 串(进日志),`detail.httpStatus`
带真实状态码。对分类 / 列表 / 详情,失败走 `softFail`(保留 message/detail,
对外 Ok)——日志照报,本地数据照常展示;仅当本地也无对应条目时才回真 fail。
播放(resolve)始终真 fail → UI 走 `showVideoError()`。

## 实测记录(2026-11-20)

- **构建**:`./buildqt3.sh` 与 `./buildqt4.sh` 均 `ret=0`(未编 Qt6)。
- **无 key 在线报错**:无 key 直接调 uNoGS → HTTP 401 +
  `{"message":"Invalid API key. Go to https://docs.rapidapi.com/..."}`;
  假 key → 403 `You are not subscribed to this API.`;连发 → 429 `Too many requests`。
- **数据集**:400 条真实条目、400 个唯一 id(`s1`..`s400`)、40 个 genre;
  临时测试程序验后已删除。
- **年份**:新增 `yearToMs()`(1970..year 逐累加天数、含闰年);
  `yearToMs(2016) == 1451606400000`,UI 32 位截断后正确显示 2016。
- **合并**:在线结果在前 + 本地数据按页窗口追加、按 id 去重;在线失败(含 401)
  时 `softFail` 仍返回本地条目,分类页显示 40 个本地 genre、列表可滚动翻页。
- **注册与文案**:Qt3 二进制含 `netflix` / `unogsng` / `QXVOTUBE_UNOGS_KEY`
  符号与字符串;三份 lang 的 `site.netflix` JSON 合法性校验通过;
  `error.unsupported` 复用既有 i18n key。
- **播放**:`resolve()` / `resolveQuality()` 固定 `Err::Unsupported` → UI 走
  `error.unsupported`(暂不支持该平台)。
- **在线路径**:无 key 也发请求并进入报错日志链路;`QLVOTUBE_DEBUG=1` 下可见
  `GET ... -> HTTP 401 ...` 的 Warn 日志,同时分类 / 列表正常显示本地数据。
- **分类时序(方案 A)**:临时 Qt3 测试验证——本地 40 genre → `categories()` 秒显
  41 项;在线优先去重后仍 41 项(首项空 key「推荐」、`Dramas`/`Comedies` 在线在前、
  与本地重名的项去重、本地 genre 补缺完整、无重复 key)。`categoriesRefresh()`
  失败时 401 只记日志、不回调,本地分类保持不动;UI 侧 `CategoriesRefresh` 事件
  就地刷新、不改 `m_gridChip`/分页、不调 `loadVideoPage`。探针验后已删除。
- **ABI 修复(同日补)**:初版把 `categoriesRefresh()` 插在 `VideoProvider` 虚表中间,
  qmake 增量构建未重编 `bilibiliprovider.o` 等,导致虚表槽位错位——启动时
  `setCredentials()` 落到错误槽位,`QMap::operator=` 对垃圾引用 `ref()` 触发 SIGSEGV。
  已把该虚方法移到虚表末尾(`cancel()` 之后)并删除 build-qt3/build-qt4 全量重编;
  Qt3/Qt4 冒烟启动均不再崩溃(exit=124 为正常超时)。**教训**:公共基类加虚方法必须
  追加在末尾,且改公共头文件后必须全量 clean rebuild。
