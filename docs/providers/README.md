# providers 视频源协议库(qxvproviders)

统一七平台视频源协议的 C++ 静态库,接入 qlvotube/qskvotube UI。

## 目录

```
providers/
├── common/       可复用基础层(与平台无关)
│   ├── compat.h          跨 Qt 版本宏(QXV_STR/QXV_SKIP_EMPTY/QXV_IEQ/QXV_ENV)
│   ├── qcjson.h          cJSON 的 QString 薄封装(JRoot/jstr/jint/...)
│   ├── debuglog.{h,cpp}  分级调试日志(QXVOTUBE_DEBUG)
│   ├── result.{h,cpp}    Err 枚举 + Result(code/message/detail,回调契约)
│   ├── errortext.{h,cpp} Err → i18n key(error.*)
│   ├── netclient.{h,cpp} cpp-httplib 封装(线程池+队列回调+代理+CookieJar
│   │                     +统一重试退避+真取消)
│   ├── quality.h        清晰度词汇(144~2160)+就近降级
│   ├── streamcache.h    直链 TTL 缓存(expire= 解析,过期自动重取)
│   ├── ratelimit.h      全局限流(按 host 最小间隔,QXVOTUBE_QPS)
│   ├── localstream/     通用本地流服务(title 无关的 Source 抽象)
│   └── hgstream/        红果 CENC 源(适配 localstream)
│   ├── cookiejar.{h,cpp} 进程内 Cookie(按域,Set-Cookie 回存)
│   └── fingerprint.{h,cpp} 浏览器/客户端 UA 档位
├── bilibili/     web REST 游客口(parse.{h,cpp} 纯解析,可离线测)
├── youtube/      InnerTube ANDROID_VR 口(search 通;player 受 IP 风控,见下)
├── tests/        smoke 联网自检(pvsmoke)+ 离线解析单测(parsetest)
│   └── fixtures/ 录制的真实响应(裁剪后)+ capture.py 重录脚本
└── factory.cpp   createProviders()
```

## 使用

```cmake
add_subdirectory(providers)          # qlvotube .pro 用户用 .pri
# 链接 qxvproviders;依赖 Qt Core OpenSSL ZLIB Threads
```

```cpp
auto provs = qxv::createProviders();
provs[0]->search("travel", 1, [](qxv::Result res,
                                 const QList<qxv::VideoItem>& items,
                                 bool hasMore) {
    // 所有回调携带 Result;失败必显示(禁 bool 静默失败)
});
// 清晰度选择(可选):resolveQuality(id, "720p", cb);空串=provider 默认。
// 词汇统一(common/quality.h):144/160/240/360/480/540/720/1080/1440/2160。
// 就近降级:实际档≠请求档(含"请求更低却给了更高")→ Stream.extra 带
//   requestedQuality + actualQuality + degraded=true;
// 完全不可播(如红果 bytevc2 ≤720p)→ Err::Unsupported / Err::NotFound。
// 代理(YouTube 等被墙源必须):setProxy() 或环境变量 QXVOTUBE_PROXY
// 重试:NetRequest.maxAttempts/retryDelayMs/retryOnEmpty
//   (传输错误/429/5xx/可选 200 空包;指数退避封顶 4s)
// 取消:provider->cancel() 精确作废本源在途/重试/待投递回调
// 单飞:同 method+url+body+jar 的并发请求只发一次,响应扇出(NetRequest.singleFlight)
// 限流:QXVOTUBE_QPS(默认 12,0=关)按 host 最小间隔,防突发被风控
// 红果 ≤720p:走官网明文 360P(bytevc2 不可播);1080p 走 App+H2b 本地解密
// 红果详情后按 QXVOTUBE_HG_PREFETCH(默认 3)后台预热前 N 集
// YouTube 高清晰度:yt-dlp(可选,自动探测)+ HLS master 裁剪 → 本地 hls 流
//   QXVOTUBE_YT_YTDLP=0/1 强制关/开;缺失自动回落 itag18 360p
// 缓存:resolve/resolveQuality 命中未过期直链直接返回,过期自动重解析
```

## 平台状态

| 平台 | 接口 | 状态 | 认证级别 |
|---|---|---|---|
| bilibili | web REST 游客口 | ✅ search(含分页)/homeFeed(热门)/detail/resolve | NoAuth |
| youtube | InnerTube ANDROID | ✅ search/detail/resolve;高清晰度走 yt-dlp+HLS(可选);⚠️ homeFeed 不可用 | NoAuth |
| hongguo | 官网公开口 + App 零签名链 | ✅ search(分页)/homeFeed(官网热播 4 类)/detail/resolve(1080p 本地解密) | DeviceAuth |
| douyin | 热榜免签(49 条);search/resolve 需 a_bogus | 🟡 homeFeed 已落码,其余 Unsupported | DeviceAuth |
| tiktok | 需 X-Bogus/msToken + 可达代理 | ⏸ 阻塞(当前代理不可达) | DeviceAuth |
| xiaohongshu | 游客 SSR(explore/detail+token/user);搜索需登录 | 🟡 homeFeed/detail/resolve 已通;search=NeedAuth;userVideos 软风控时降级 | NoAuth |
| xiaohongshu/kuaishou | | P3 | CookieAuth |
| xigua/haokan/acfun/migu | | P4 | |

## 关键决策

- **JSON 一律 cJSON**(qlcomp 同源份,Qt3 兼容已验证),禁 QJson(Qt3 无此 API)。
- **字符串全 UTF-8**:`QXV_STR` 宏统一;禁 QLatin1String/QLatin1Char/裸 QStringLiteral。
- **Qt3 构建整体排除 providers**,引用方走 stub;providers 库本体 Qt>=5。
- **错误传导**:所有回调 `Result{Err 枚举, message, detail}`;UI 用 errortext 的
  `error.*` i18n key 显示;真人识别走 `CaptchaRequired`(detail.captchaUrl)弹框。

## 调研文档(扁平,文件名带站点)

- `xiaohongshu-research.md` —— 小红书免登录可行性/风控阈值/可实现范围
- `debugging.md` —— 跨平台踩坑记录(httplib/线程/重试/缓存)
- `design.md` —— 总体设计
- `ui-integration.md` —— qlvotube 接入:站点 combo / 动态分类 / 网格翻页 / 封面缓存

### 错误模型(认证感知,2026-11-15)

| detail key | 含义 |
|---|---|
| `auth.mode` | `none` / `cookie` / `oauth` / `password` |
| `auth.provided` | 是否已设置凭证 |
| `auth.expired` | 服务端是否判失效 |
| `http.status` / `server.code` / `server.message` | 原始响应(消息已脱敏) |

非认证错误用 `withServer(Result, httpStatus, serverCode, serverMsg)` 附加原始信息
(不写 `auth.*`)。

**各源错误映射**

| provider | 平台信号 → Err |
|---|---|
| bilibili | `-101`→`NeedAuth`(有凭证则 `AuthExpired`)、`-412`→`RateLimit`、`-404`→`NotFound` |
| youtube | `LOGIN_REQUIRED`→`NeedAuth`/`AuthExpired`、`captcha/not a bot`→`CaptchaRequired`、`UNPLAYABLE`→`GeoBlocked` |
| hongguo | web `403/400 public_fallback_unavailable`→`Paywalled`；App `code!=0`→`Parse` |
| douyin | 未实现能力→`Unsupported`(附 `auth.mode`);热榜 `status_code!=0`→`Parse` |
| xiaohongshu | 搜索→`NeedAuth`；`302→/login`→`CaptchaRequired`；`406/461`→`RateLimit` |

`errortext::i18nKeyFor(Result)` 优先按认证维度选键:
`error.auth_guest_need_login` / `auth_cookie_missing` / `auth_cookie_expired` /
`auth_oauth_expired` / `auth_password_failed`；**凭证明文绝不进 message/detail**。

## 引用来源

| 资料 | 用途 |
|---|---|
| bilibili-API-collect | bilibili REST 口径/风控码 |
| yt-dlp / pytubefix | InnerTube client 档位与 body 口径 |
| iv-org/invidious(#4734/#4789 + errors-explained) | YouTube IP 风控与 PO token 方案 |
| iv-org/youtube-trusted-session-generator | po_token/visitor_data 生成 |
| BGutil-youtube-pot-provider | ANDROID_VR+PO token 解 datacenter IP |
| AlvaroBalbin/yt-innertube | ANDROID_VR 免 decipher 直出 url 参考 |
| cpp-httplib (vendored) | HTTP/TLS/代理 |
| DaveGamble/cJSON (qlcomp) | JSON 解析 |

## 测试分层(2026-11-15)

| 层 | 入口 | 是否需要外网 | 说明 |
|---|---|---|---|
| 离线单测 | `ctest` → `providers_parse` / `providers_reliability` / `providers_hgcenc` / `providers_errortext` / `providers_autherror` / `providers_xhssign` / `providers_xhsapi` | ❌ 不需要 | fixture 回放解析 + 重试/取消/缓存 + CENC 向量 + 认证错误模型 + xhs 签名器(1000 组差分) |
| 联网自检 | `./pvsmoke [平台] [-q 720p]` | ✅ | search→detail→resolve→流头 1MB |
| 专项 | `./paging` `./homefeed` `./detail` `./quality` `./hgapp` `./hgstream` | ✅ | 分页/homeFeed/详情/清晰度/红果 E2E/本地代理 |
| Qt3 纯 C++ 件 | `providers/tests/qt3sign/build.sh` | ❌ 不需要 | 用 Qt 3.3.8sh 构建 sign+api 并跑 KAT/1000 组差分/API 解析 |

### fixture 重录
```bash
QXVOTUBE_PROXY=http://127.0.0.1:10809 \    # YouTube 需要
  python3 providers/tests/fixtures/capture.py          # 全部
  python3 providers/tests/fixtures/capture.py bilibili # 单平台
```
裁剪策略:数组留前 2~3 项、字符串截断 300 字符、丢弃 tracking/logging 等噪声字段,
YouTube 只保留解析所需子树(MB → 几十 KB)。**bilibili 搜索 fixture 先筛出
`result_type=="video"` 分组再裁剪**(否则会被其它分组挤掉)。

## 构建接入(2026-11-15)

### CMake(Qt>=5,推荐)
```cmake
add_subdirectory(providers)
target_link_libraries(myapp PRIVATE qxvproviders)
```

### qmake(`providers/providers.pri`)
```qmake
include(../providers/providers.pri)
```
| 工具链 | 行为 |
|---|---|
| **Qt >= 5** | 编全部实现(含 `hgapp`/`hgstream`/四个 `parse.cpp`)+ `qlcomp/compatcore34.cpp` + `cJSON.c`;需 OpenSSL/zlib/pthread;`QT += core gui` |
| **Qt 4** | **stub**:只编 `provider_stub.cpp` → `createProviders()` 返回空表 |
| **Qt 3** | **stub**(自动 `DEFINES += QT3_BUILD`、`-std=c++14`、补 `QT3_BUILD` 头名/`QVariantMap`) |

要点:
- Qt>=5 分支带 `CONFIG += object_parallel_to_source`:四个 `parse.cpp` 同名,
  不分目录会互相覆盖(实测 undefined reference)。
- 宿主已编过 qlcomp 公共件时:`DEFINES += QXV_PROVIDERS_NO_QLCOMP` 可跳过
  `compatcore34.cpp` / `cJSON.c`(qlvotube 已自带 cJSON.c)。
- **Qt3/Qt4 能力为 0**:UI 应据 `createProviders().isEmpty()` 隐藏视频源入口,
  不显示"空源"。
- 接入验证工程:`providers/tests/qt3stub/`(同一份 .pro 分别用
  3.5.0 / 4.8.7 / 6.7.3 qmake 构建,实测 providers=0/0/4)。
