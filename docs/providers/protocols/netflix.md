# Netflix 协议文档(目录展示 + 无源报错)

## 定位

Netflix 无官方公开 API;正片需订阅登录且受 Widevine/PlayReady/FairPlay DRM 保护,
拿不到可播放直链。本 provider **只做目录展示**(分类 + 列表 + 详情),不提供播放流:
`resolve()` / `resolveQuality()` 固定返回 `Err::Unsupported`(UI 复用
`error.unsupported`:暂不支持该平台)。

公开 trailer 托管在 YouTube/Tudum,不属于 Netflix 站内流,不在本 provider 范围内。

## 数据来源:两者结合

1. **在线**:uNoGS(unogsng, RapidAPI),每日抓取 30+ 国家目录。
2. **本地兜底**:编译进二进制的精选标题集(`netflix_data.cpp`),可替换为
   netflix_titles.csv 全量(~8800 条)转换结果。

策略:**优先在线,失败/无 key 自动回退本地**。分类/列表/详情均无感降级;
本地集无封面图 URL(UI 显示占位),在线集有 Netflix CDN 海报。

## 认证:key 可选(重要)

- **不需要** Netflix 账号/订阅登录(uNoGS 的价值就是免订阅查目录)。
- **RapidAPI key 可选**:仅用于提升数据新鲜度与覆盖面。
  - 无 key → 完全走本地数据集,功能不报错;
  - 有 key → 走 uNoGS 在线拉取;
  - key 失效/额度用尽/网络失败 → 自动回退本地,不当作用户错误。
- 获取:RapidAPI 注册账号 → 订阅 `unogsng` BASIC 免费层 → 复制 key。
- 提供方式:**环境变量 `QXVOTUBE_UNOGS_KEY`**(不硬编码、不设登录 UI)。

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

## 映射规则

- `genres` → `CategoryItem`(空 key = 「推荐」走 homeFeed);本地路径从
  数据集 genre 字段派生。
- `results[]` → `VideoItem`:`nfid→id`、`title→title`、`boxart→cover`、
  `year→publishMs`、直接拼 `webUrl = netflix.com/title/<id>`。
- 分页:`limit=20`、`offset=(page-1)*20`,`hasMore = 本页满 20 条`。

## Err 映射

| 场景 | Err |
|---|---|
| 无 key / 在线失败 / 解析空 | 回退本地,`Result::ok()`(不算错误) |
| 本地未找到条目(detail) | `NotFound` |
| 播放(resolve/resolveQuality) | `Unsupported` |

## 实测记录(2026-11-20)

- **构建**:`./buildqt3.sh` 与 `./buildqt4.sh` 均 `ret=0`(未编 Qt6)。
- **无 key 本地兜底**:临时 Qt3 测试程序实测 `netflixLocalTitles()` 返回 22 条、
  id 无重复;`netflixLocalGenres()` 派生 12 个 genre;`netflixFindLocal()`
  正向/未命中均正确。测试程序验后已删除。
- **注册与文案**:Qt3 二进制含 `netflix` / `unogsng` / `QXVOTUBE_UNOGS_KEY`
  符号与字符串;三份 lang 的 `site.netflix` JSON 合法性校验通过;
  `error.unsupported` 复用既有 i18n key。
- **播放**:`resolve()` / `resolveQuality()` 固定 `Err::Unsupported` → UI 走
  `error.unsupported`(暂不支持该平台)。
- **在线路径**:需 `QXVOTUBE_UNOGS_KEY`,本次无 key 未实测;
  代码保证在线失败/解析空自动回退本地,不向用户报错。
