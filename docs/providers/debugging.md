# providers 调试手册

## 环境变量

| 变量 | 作用 |
|---|---|
| `QXVOTUBE_DEBUG` | 模块/级别过滤:`"http"` / `"http,youtube:debug"` / `"bilibili:trace"`;级别 err/warn/info/debug/trace |
| `QXVOTUBE_DEBUG_FULL` | 打开全量(含敏感字段,自动 mask 脱敏) |
| `QXVOTUBE_PROXY` | `http://host:port`,YouTube/TikTok 等被墙源必须;本机测试口径 `http://127.0.0.1:10809` |

## 输出格式

```
[PROVIDERS][http][Debug] POST https://www.youtube.com/youtubei/v1/player -> status=200 bytes=975 [netclient.cpp:116]
[PROVIDERS][youtube][Err] POST ... failed: Connection timed out [netclient.cpp:105]
```

## smoke 自检

```bash
cd providers
QXVOTUBE_PROXY=http://127.0.0.1:10809 ./build/tests/pvsmoke          # 全平台
QXVOTUBE_PROXY=http://127.0.0.1:10809 ./build/tests/pvsmoke bilibili # 单平台
# rc: 0=全绿 1=有 FAIL 2=超时(90s)
```

三步链:search → resolve → **streamhead**(`Range: bytes=0-1048575`,
YouTube 的 googlevideo 域自动走代理;bilibili 直连)。

## 常见故障速查

| 现象 | 根因/修法 |
|---|---|
| 400 "plain HTTP request was sent to HTTPS port" | httplib 用了 `Client(host,port)`;必须 `Client("https://host")` scheme 形式 |
| `undefined reference to inflateInit2_` | 没链 ZLIB |
| `undefined reference to vtable for ...` | Q_OBJECT 类没被 AUTOMOC |
| bilibili search -412 | 没预热首页/缺 Referer/UA |
| youtube search 慢但 200,items=0 | 响应是风控变体;开 `youtube:trace` 看 body 头部 |
| youtube player LOGIN_REQUIRED(not a bot) | IP 级风控,见 protocols/youtube.md;不是代码问题 |
| smoke TIMEOUT | 异步链上 done() 漏调(pending 计数挂起);检查失败分支是否调 done() |
| streamhead 无输出即退出 | resolve 成功分支后的 done() 提前触发;done 只能由最终回调调用 |

## Qt3

- providers 库本体不参与 Qt3 构建;引用方(CMake)排除或走 stub。
- 需要 Qt3 兼容的公共头(qcjson.h/compat.h)遵守:
  头名小写(`qstring.h`)、`QXV_STR`、`qbaFromCharptr()`(compatcore34,Qt3 无
  `QByteArray(const char*)` 构造)、比较用 `QXV_IEQ`。

## httplib 隐坑:content_type 参数替换 Content-Type(2026-11-15 红果 400 根因)

httplib `Client::Post(path, headers, body, len, content_type)` 内部走
`send_with_content_provider`,其中 `req.set_header("Content-Type", content_type)`
是**替换**语义——headers 里已设的 `application/json; charset=UTF-8` 会被
默认值 `application/octet-stream` 盖掉。

- 实测现象:红果 `fallback-play-info` 用 curl 同口径(同头同 body)200,
  provider 里恒 400 `{"error":"public_fallback_unavailable"}`(39 字节)。
- 结论:该 400 错误体同时是"Content-Type 不对"与"试看集外"的表现,
  排查时先用 curl 分离变量再对比实际发出头。
- 修复:netclient.cpp 里 headers 已含 Content-Type 时给 httplib 传空串
  (`ctype = hasCt ? "" : "application/octet-stream"`)。
- 通用教训:凡是带 body 的 POST,Content-Type 一律在 headers 里显式给,
  不要依赖 httplib 的 content_type 参数。

## httplib 隐坑:Server 端 ContentProvider 三坑(2026-11-15 hgstream)

1. **不要自己调 `sink.done()`**:`DataSink.done` 由 server 在
   `write_content_with_provider` 内部绑定;provider 里调用 → `std::bad_function_call`
   (进程直接 abort)。provider 只需 `sink.write(...)` + `return true`。
2. **测试 TU 直接 include `common/httplib.h` 必须带与库相同的编译定义**
   (`CPPHTTPLIB_OPENSSL_SUPPORT`/`CPPHTTPLIB_ZLIB_SUPPORT`),否则头文件内联实现
   跨 TU 不一致(ODR)→ 运行时 `SSLClient::process_socket` 断言 `socket.ssl` 失败。
   修法:`target_compile_definitions(<test> PRIVATE ...)` 对齐。
3. **`Client(host, port)` 恒明文**,TLS 必须用 `Client("https://host:port")`
   或 `SSLClient`;本地服务用明文形式即可。
4. **Range/206/416 无需手写**:`set_content_provider(total, type, provider)`
   已内置;provider 的 `(offset,length)` 即请求区间。

## 线程退出顺序:先置标志再 join(2026-11-15 hgstream 退出挂死)

`stop()` 里若先 `join()` 看门狗线程、后置 `running=false`,看门狗
`while (running)` 永不退出 → join 永久阻塞(现象:pvsmoke 全部断言打印完但进程
不退出,被外部 timeout 杀掉)。正确顺序:

1. 持锁置 `running=false`,发条件变量通知;
2. `srv.stop()`;
3. 逐个 `join()`(join 时不持锁,避免与 handler 抢锁死锁);
4. 最后清会话/释放资源并打日志。

## YouTube:空结果/风控的真实根因是客户端口径(2026-11-15)

- **客户端与 UA 必须同源**:body `clientName=WEB` 配 Android UA → 恒空;
  ANDROID body + Android UA → 正常(compactVideoRenderer)。
- **cookie 与 X-Goog-Visitor-Id 互斥**:带预热 cookie 再带访客头 → `backgroundPromoRenderer`;
  关掉 cookie(`autoCookie=false`)即恢复。
- **HTTP 200 + 232B `{"error":{"code":500,"backendError"}}`** 是上游偶发抖动,
  必须重试;否则表现为"搜索 0 条"(不是空结果,是错误)。
- 空结果不要静默返回 `Ok+0 条`,应 `Err::RateLimit` 并带重试次数。

## hgstream:测试 TU 与库的 httplib 编译定义必须一致

详见 bytedance.md §7.4;`tests/CMakeLists.txt` 里对直接 include `httplib.h` 的
测试目标补 `CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT`,否则 ODR 冲突
导致运行时 `SSLClient::process_socket` 断言 `socket.ssl`。
