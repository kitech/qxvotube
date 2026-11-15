# providers 设计文档

## 1. 分层

```
UI(qlvotube/qskvotube)
  ↓ 回调(Result 透传)
provider 层(bilibili/youtube/...)   协议解析、风控映射
  ↓ NetRequest / NetResponse
common/netclient(httplib 封装)      TLS/代理/Cookie/线程池
  ↓
cJSON(qlcomp) / OpenSSL / ZLIB
```

## 2. 线程模型

- `NetClient` 内置 `QThreadPool`(4 线程);httplib 是阻塞型,每请求占一线程。
- 回调经 `QMetaObject::invokeMethod`(QueuedConnection)回到发起线程,
  UI 层无需加锁;provider 侧 `static NetClient` 进程级共享。
- smoke/测试中异步链的 `NetClient` 必须 `static`(栈上对象会先于回调析构)。

## 3. 错误传导(核心契约)

```
Err 枚举:Ok/Network/Tls/Http/Parse/RateLimit/CaptchaRequired/RiskHint/
         NotFound/NeedAuth/AuthExpired/Paywalled/GeoBlocked/Unsupported/Cancelled
Result  :{code, message, detail(QVariantMap)}
Result::retriable():Network/RateLimit/CaptchaRequired → UI 可重试
```

- 每个回调签名都带 `Result`,**禁 bool 返回静默失败**。
- `errortext.cpp` 做 Err → i18n key(`error.*`)映射;三语 JSON 键组由
  `tests/errortext.test` 锁定。
- **真人识别传导**:`CaptchaRequired` + `detail.captchaUrl`;
  UI 弹"需要人机验证"对话框 → 打开 challenge → 回填 cookie(接入阶段实现)。

## 4. 调试日志(debuglog)

- 分级:Err/Warn/Info/Debug/Trace;模块过滤:`QXVOTUBE_DEBUG="http,youtube:debug"`。
- `QXVOTUBE_DEBUG_FULL` 打开全量;敏感头(mask)脱敏后输出。
- 输出统一 `[PROVIDERS][module][Level] msg [file:line]`。

## 5. 跨版本兼容(compat.h)

| 宏 | 解决 |
|---|---|
| `QXV_STR("...")` | Qt>=5.2 → QStringLiteral;更低 → fromUtf8。**全库禁 QLatin1*/裸 QStringLiteral** |
| `QXV_SKIP_EMPTY` | Qt<5.14 的 `QString::SkipEmptyParts` 位置差异 |
| `QXV_IEQ(a,b)` | Qt3 无 `compare(QString,Qt::CaseInsensitive)`,统一 toUpper 比较 |
| `QXV_ENV/QXV_ENV_EMPTY` | Qt<5.10 无 qEnvironmentVariable |

- providers 库仅在 Qt>=5 构建;Qt3 引用方拿空实现 stub(`qxv_compat_guard.h`)。
- JSON:cJSON(qlcomp 唯一份)→ Qt3 可编译;**禁 QJson**。

## 6. HTTP 细节(踩坑记录)

- httplib v0.20:`Client("https://host")` scheme 形式才走 TLS;
  `Client(host, port)` **恒为明文**(400 The plain HTTP request was sent to HTTPS port)。
- ZLIB 缺失 → `undefined reference to inflateInit2_`;CMake 必须链 `ZLIB::ZLIB`。
- Q_OBJECT 类必须 `CMAKE_AUTOMOC ON`(缺了 → vtable undefined)。
- 代理:`NetRequest.proxy = "http://host:port"`;来源为 setProxy() 显式设置
  或 `QXVOTUBE_PROXY` 环境变量(YouTube/TikTok 被墙必须)。

## 7. 风控与 IP 级封锁

- YouTube `LOGIN_REQUIRED: Sign in to confirm you're not a bot` 是 **IP 级**风控
  (数据中心出口 IP 全员命中,与代码无关;curl 复测同样现象)。
- 已验证无效:ANDROID_VR+visitorData、TVHTML5、带 cookie、TVEMBED。
- 已验证有效(此前):住宅/干净 IP 上 ANDROID_VR 直通(免 key/PoToken)。
- 解法(P2):BGutil PO token(BotGuard attestation)或住宅代理,
  见 protocols/youtube.md 引用来源。
