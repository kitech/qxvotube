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
│   ├── netclient.{h,cpp} cpp-httplib 封装(线程池+队列回调+代理+CookieJar)
│   ├── cookiejar.{h,cpp} 进程内 Cookie(按域,Set-Cookie 回存)
│   └── fingerprint.{h,cpp} 浏览器/客户端 UA 档位
├── bilibili/     web REST 游客口(已全链通)
├── youtube/      InnerTube ANDROID_VR 口(search 通;player 受 IP 风控,见下)
├── tests/        smoke 双平台自检(pvsmoke)
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
// 代理(YouTube 等被墙源必须):setProxy() 或环境变量 QXVOTUBE_PROXY
```

## 平台状态

| 平台 | 接口 | 状态 | 认证级别 |
|---|---|---|---|
| bilibili | web REST 游客口 | ✅ search/detail/resolve 全链通 | NoAuth |
| youtube | InnerTube ANDROID_VR | ⚠️ search 通;player 需 P2 PO token | NoAuth |
| hongguo/douyin/tiktok | bytedance 签名栈 | P2 | DeviceAuth |
| xiaohongshu/kuaishou | | P3 | CookieAuth |
| xigua/haokan/acfun/migu | | P4 | |

## 关键决策

- **JSON 一律 cJSON**(qlcomp 同源份,Qt3 兼容已验证),禁 QJson(Qt3 无此 API)。
- **字符串全 UTF-8**:`QXV_STR` 宏统一;禁 QLatin1String/QLatin1Char/裸 QStringLiteral。
- **Qt3 构建整体排除 providers**,引用方走 stub;providers 库本体 Qt>=5。
- **错误传导**:所有回调 `Result{Err 枚举, message, detail}`;UI 用 errortext 的
  `error.*` i18n key 显示;真人识别走 `CaptchaRequired`(detail.captchaUrl)弹框。

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
