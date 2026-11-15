# bytedance 系协议文档(红果/tiktok/douyin)

> P2 调研产物(2026-11)。先读本文档再动码;**实现分期见文末路线图**。

## 0. 总览:两条通道

| 通道 | 签名 | 能力 | 风险 |
|---|---|---|---|
| ① 官网公开口(hongguoduanju.com) | **无签名**,普通 HTTP | 元数据全通(搜索/详情/vid_list);播放仅前 3 集 | 付费墙(第 4 集 403) |
| ② App 协议(novel.snssdk.com 等) | device_register + 五头 | 全集播放;CENC 解密可纯离线 | 签名栈移植量大 |

## 1. 红果(hongguo)

### 1.1 官网公开口(通道①,2026-10 实测,dudt/HongGuoFree)

| 能力 | 接口 | 状态 |
|---|---|---|
| 搜索 | `GET https://hongguoduanju.com/api/search/result?query=` | ✅ JSON(剧名/封面/简介/热度/分类) |
| 详情 | `GET /detail?series_id=xxx`(HTML) | ✅ HTML 内嵌 `vid_list`(实测一部剧 180 个 vid) |
| 播放地址 | `POST /api/video/protected/play-info` | ⚠️ 仅前 3 集;第 4 集起 `403 {"error":"protected_playback_unavailable"}` |

`play-info` 请求体(**字段名是 item_id 不是 vid**;definition 只支持 `720p`/`480p`):

```http
POST https://hongguoduanju.com/api/video/protected/play-info
Content-Type: application/json
Referer: https://hongguoduanju.com/player/<series_id>/<vid>
Origin: https://hongguoduanju.com

{"series_id":"7695727473111092286","item_id":"7695729394496588862","definition":"720p"}
```

返回 `play_info.main_url`(⚠️ 2026-11 实测:现返回 **DASH**——视频/音频两条
独立 BaseURL + player_access_key,单 URL 拿不到完整可播文件)、`kid`、`expires_at`。
CDN 域名:`v26-hgweb.qznovelvod.com/...`,`206 Partial Content` 可拉但 **CENC 加密**
(`encv`/`sinf` box)。19 位 ID 一律按**字符串**处理(禁浮点)。

### 1.1b fallback-play-info(★ H1 主通道,2026-11 实测)

```http
POST https://hongguoduanju.com/api/video/protected/fallback-play-info
Content-Type: application/json; charset=UTF-8
User-Agent: Mozilla/5.0 ... Chrome/126 ...

{"item_id":"<vid>"}     ← 只需要 item_id,连 series_id 都不要
```

- 返回 `{main_url, backup_url, url_expire}`:**明文渐进式 mp4**(avc1+mp4a 双轨、
  完整时长、CDN 支持 Range)——**无需任何解密即可直连播放**
- definition 参数不生效,固定 360P(公开回退档)
- 试看集放行(`accessible_episode_cnt`,通常前 3 集);VIP 集
  `403 protected/public_fallback_unavailable`(正常拒绝,不重试)→ Paywalled
- 官网 UA 实测只需 User-Agent,不带头也通,带上更稳

### 1.2b 官方 web 播放器逆向侦察(2026-11-15 实测,★★ H2 定案依据)

720p DASH 密文流的官方解密链拆解(实测抓 bundle + 抽 worker):

| 环节 | 实现 | 结论 |
|---|---|---|
| play-info 响应 | `player_access_key`(base64 解出 37B,非 16B 明文 key)+ `kid` | 密钥不是明文给的 |
| 播放页 chunk | `DASHPlugin{secretKey=player_access_key, useEME:false, encrypt:true}` | MSE 插件自解密 |
| 密钥/解密逻辑 | JS 字节码 VM 混淆(`wm("504B0101…")` 解释器)+ **WASM**(`_descrypt`/`_decryptKey`/`_descryptCBC`/`_descryptKdsKey`/`_openBox`) | worker 内 WASM 解密 |
| key 来源 | `getLicenseUrl` 默认空;本站走 secretKey 直传 → worker `updateValue` → WASM | KDS/WASM 内变换 |

**结论:web 通道 player_access_key → 内容密钥的变换被 JS VM + WASM 双重包住,
离线复现成本高且随时变版,不可行。novelquickapp.com 已 301 并回 hongguoduanju.com
(无独立明文口);/player SSR 页亦无 main_url(10 月口径已失效)。**

**因此 H2(解密引擎)与 H3(App 协议签名)必须合并推进:**
- 密钥/取流的唯一 OSS 可复现路线 = App 通道 `video_model/v1` → `spade_a`
  → 纯算法 unwrap(无 KEK,零依赖)→ 16B key → AES-128-CTR per-sample
  → MP4 容器重写(去 sinf/encv→avc1)。
- 引擎部分(spade/CENC-CTR/容器重写/本地代理)与取流部分(device_register+五头签名)
  分两个子步落码,引擎先行,签名跟后;黄金向量用 zhangbaio/hongguo 纯 Python
  流程与 hongguo-open-player cenc/spade.go 口径交叉锁定。

### 1.2 App 协议(通道②)

**设备注册**(参考 zfdang/hongguo-open-player,推断性文档,未源码验证):
- `POST https://is.snssdk.com/service/2/device_register/`
- body 需 **TTEncrypt V5** 加密:魔数头 `tc05` + Gzip + 派生密钥 AES-CBC
- 换取 `device_id`/`install_id`,持久化(设备档案禁每请求换)

**五头签名**(x-gorgon/x-argus/x-ladon/x-helios/x-medusa,hongguo-desktop 有
黄金向量单测锁定):
- `X-SS-Stub: MD5(body)`、`X-Khronos: unix秒`、`X-SS-Req-Ticket: unix毫秒`
- X-Gorgon:RC4 变种 + 位反转 + 移位混淆(输入 = query + body hash)
- X-Argus 依赖固定 AES-128 调度路径(danmu_api #406 印证)
- **签名缺失/算错时服务端返回 HTTP 200 + 0 字节**——判错要看 body 字节数

**接口清单**(App host,来自 hongguo-open-player/hongguo-desktop):

| 用途 | 路由 |
|---|---|
| 搜索 | `/reading/bookapi/search/tab/v`(suggest: `/search/suggest/v`) |
| 频道/推荐 | `/reading/bookapi/bookmall/tab/v`、`/bookmall/cell/change/v1/` |
| 分类榜单 | `/reading/distribution/category/landpage/v1/` |
| 单集播放 | `/novel/player/video_detail/v1/`(清晰度列表+直链+加密字段) |
| 多集批量 | `/novel/player/multi_video_model/v1/` |
| 进度上报 | `/reading/bookapi/read_progress/upload/v` |
| 书架 | `/reading/bookapi/bookshelf/video/list/v`(update) |
| 评论/弹幕 | `/novel/commentapi/comment/list/`(add) |
| 短信登录 | `/passport/mobile/send_code/v1/`、`/passport/mobile/sms_login/` |

⚠️ 2026-11 实测(dong/fun hongguo.py):**`multi_video_model/v1/` 已停服**
(任何请求回 200 空包);同主机生效的是 **`video_model/v1/`**(请求体改顶层
`video_id`)。App host:`api5-normal-sinfonlineb.fqnovel.com` /
`api5-normal-lf.fqnovel.com`(双主机轮换)。空响应判断:HTTP 200 +
Content-Length 0 = 接口关闭或需登录态,**必须显式报错**,不能当空数据成功放行。
App UA 口径:`com.phoenix.read/71332 (Linux; U; Android 16; zh_CN; ...)`
(main_url/backup_url_1 现为 base64 包装的明文直链,旧版可能是 spade 密文,保留解密回退)。

### 1.3 CENC 解密(核心难点,已可纯离线)

视频 = ISO/IEC 23001-7 CENC,**AES-128-CTR 按样本解密**(zhangbaio/hongguo
100% 纯 Python 复现,自测 5/5):

```
multi_video_model → video_list[].main_url        (CDN 密文直链)
                  → video_list[].encrypt_info.spade_a (★ 包装的内容密钥)
unwrap_spade(spade_a) → 16B content key          (纯字节变换,无 KEK/无 AES/无 app)
密文 mp4 的 senc 盒 → base_iv64(8B)
AES-128-CTR(key, IV=((base_iv + 样本序号) << 64)) 逐样本解密 → 明文 mp4
```

spade_a 派生算法(hongguo-open-player cenc/spade.go 口径):
1. 变种 Base64 解码;2. 首部 3 字节异或为初级种子;
3. 遍历密文字节流,popcount 动态偏移 + 字节加减移位混淆;
4. 产出 32 字符 hex(非 hex 即错)→ 转 16B 密钥。

MP4 容器重构:去掉 `moov.trak.mdia.minf.stbl.stsd` 内 `sinf` 盒,
FourCC `encv`/`enca` → `avc1`/`hvc1`;流式播放需按 sample offset 对齐 Range。

### 1.4 Err 映射

| 现象 | Err |
|---|---|
| 第 4 集起 protected_playback_unavailable 403 | Paywalled |
| 签名错 → 200 空体 | Parse(detail 补"签名可能错误") |
| 设备注册失败 | NeedAuth |

## 2. tiktok / douyin(待调研,P2 后期)

- 同为字节栈:device_register + 五头签名复用红果 signer;
- douyin 需 webid/verify_fp 口径;tiktok 需代理(同 YouTube)。
- 参考:reversetok、TikTokApi、parse-video、PolyDL(引用来源表待补调研)。

## 3. 实施路线图(分期)

| 期 | 内容 | 依赖 |
|---|---|---|
| **H1** | hongguo provider:搜索+详情(通道①无签名);resolve 走 **fallback-play-info → 明文直链(前 3 集可播)**;VIP 集 → Paywalled | 无 |✅ 已完成 |
| **H2a** | **CENC 解密引擎**(纯离线):spade_a unwrap + AES-128-CTR per-sample + MP4 容器重写(去 sinf/encv→avc1) + 黄金向量单测(先用 zhangbaio Python 流程生成向量) | H1 |✅ 2026-11-15 完成(unwrap 5 真值 + 合成 CENC roundtrip 全绿;E2E 真实向量待 H3a)
| **H2b** | 本地流式解密代理(HTTP Range 兼容 + 按需解密),player 直连本地口播明文 | H2a |✅ 2026-11-15 完成(`common/hgstream/`:moov 同长度改写 + 样本窗口解密 + 会话引用计数/空闲关 + ffmpeg 真解码 30 帧全绿) |
| **H3a** | App 协议取流:**零签名路线**(信封 device_register + `multi_video_model/v1` 游客口径,`video_platform:1024`+`need_all_video_definition`)→ 1080p bytevc1 spade_a + 密文直链 | H2a |✅ 2026-11-15 完成(`common/hgapp/` + `tests/hgapp.test.cpp` E2E 全绿:5 档轨道/1080p 175/175 NAL 自证) |
| **H3b** | 引擎+取流联通:全集高清可播 | H2b/H3a |✅ providers 侧 2026-11-15 达成(resolve 出 1080p 本地可播 URL,smoke 三步链全绿);UI 接入待 qskvotube |
| **T1** | tiktok:device_register + 签名复用 + 代理 | H3 |
| **D1** | douyin:同上 + web 口径 | H3 |

**H1 即可交付:红果搜索/详情/前 3 集可播(明文直链),VIP 集 UI 显示"付费内容"。**
高清/全集依赖 H2/H3,按验收节奏推进。

## 引用来源

| 资料 | 链接 |
|---|---|
| hongguo-desktop(五头签名+CENC 引擎,Rust) | https://github.com/it00021hot/hongguo-desktop |
| hongguo-open-player(架构文档,推断性) | https://github.com/zfdang/hongguo-open-player |
| HongGuoAPI(43 条接口清单+验证矩阵,Go) | https://github.com/xycvip/HongGuoAPI |
| hongguo-downloader(CENC 引擎,Electron) | https://github.com/zhenyong97/hongguo-downloader |
| HongGuoFree(官网口实测) | https://github.com/dudt/HongGuoFree |
| zhangbaio/hongguo(spade 纯离线解密,Frida 预言机) | https://github.com/zhangbaio/hongguo |
| hongguo-dl(2026-09 风控迁移记录) | https://github.com/gybeyond1/hongguo-dl |
| danmu_api(X-Argus AES-128 路径) | https://github.com/huangxd-/danmu_api |
| dong/fun hongguo.py(2026-11 fallback-play-info 实测) | https://gitea.lihaso.com/dong/fun/src/branch/main/py/hongguo.py |
| hongguo-gui(Linux 客户端,unidbg 签名+免签发现面,2026-10 实测) | https://git.lumorian.org/Lumorian/hongguo-gui |
| fanqie-dl(Rust 六头签名纯算法,番茄=SHA-256 系) | https://github.com/linzj/fanqie-dl |
| hongguo-desktop issue #742(公开游客画像无签名取 multi_video_model) | https://github.com/waligoraamodio288-rgb/hongguo-desktop-releases/issues/742 |

## 6. 网搜核对(2026-11-15,对 H3a 侦察结论的修正)

### 6.1 修正:「网关必须真机登录态」结论被部分推翻 ⚠️

**★ 2026-11-15 打样实测定案:全链路打通,零签名、零真机。**可用配方:

```
① device_register(信封格式,无签名):
   POST https://log.snssdk.com/service/2/device_register/?aid=8662&app_name=phoenix_read&version_code=73532&device_platform=android
   body = {"magic_tag":"ss_app_log","header":{机型/系统/openudid/cdid/display_name:"phoenix_read",app_name:"phoenix_read",...},"_gen_time":ms}
   → 200 {device_id, install_id, device_token, new_user:1}   ← 实测即得
② multi_video_model(无签名,游客口径):
   POST https://api5-normal-sinfonlineb.fqnovel.com/novel/player/multi_video_model/v1/?<公共query,带 device_id/install_id>
   body = {"dr_scene":"preload","biz_param":{"video_platform":1024,"need_all_video_definition":true},
           "mixed_video_id_map":{"1":[vid]}}
   → code=0,5 档轨道(360p~1080p 全带 spade_a);video_platform 只有 1024 合法(0~4 全拒 100001)
③ CDN 密文 mp4(moov 前置,encv/enca + stbl 内非标准 senc) → spade unwrap → AES-CTR 逐样本
   实测:1080p bytevc1(HEVC) 175/175 样本 NAL 自证通过;720p bytevc2 705/705 通过
```

关键口径:
- **单集 `video_model/v1` 已死**(200 空包,所有身份);**多集 `multi_video_model/v1` 活**(带 mixed_video_id_map)
- `definition` 值带 `p` 后缀("1080p");1080p=bytevc1(HEVC 通用可播),≤720p=bytevc2(私有编码,不可解)
- 同一集所有清晰度共享同一 content key(spade 同值)
- 无签名也不需要 x-ss-stub/gzip——**此前空响应的根因是 video_platform=3 与单集端点,不是签名缺失**
- 同 IP 重复注册返回同一 device_id(`new_user:0`,服务端按 IP/机型去重),对取流无影响
- 落码:`common/hgapp/hgapp.{h,cpp}`(register/videoModel/pickBest,设备进程内缓存 + `QXVOTUBE_HG_DEVICE_ID`/`_INSTALL_ID` 覆盖);`tests/hgapp.test.cpp` 手动跑(需外网,不进 ctest)
- **H3a-1 六头签名 C++ 移植降级为可选项**:当前零签名路线可用,仅风控收紧时再启(口径源 dong/fun 2026-11)

社区 2026-10~11 多处实测显示 fqnovel 播放接口**并非只能真机设备**:

| 来源 | 实测 |
|---|---|
| **hongguo-gui**(2026-10-08) | `search/multi_video_model` **游客开放,不校验 device_id 是否注册**——随机设备+签名即可 code=0;且 **device_register 信封格式可离线注册**(无需真机,`{magic_tag:"ss_app_log",header:{...},_gen_time}`,host `log.snssdk.com/service/2/`,header 用 `app_name:"phoenix_read"`/`device_model`,扁平 payload 被拒 device_id=0) |
| **hongguo-desktop #742**(2026-11) | **公开游客画像(7.3.5.32/update_64)+ 无签名** 调 `multi_video_model/v1` → code=0,5 档轨道(360p–1080p)全带 spade_a;**带签名身份反被静默空 200**("审计游客");preload 路径两种身份都空 |
| **hongguoTV #16/PR#23**(2026-10-13) | `multi_video_model/v1` 单集接口签名直连可用;修复方案 = "注册设备 + 真实 Argus/Ladon 签名" |
| **hongguo-dl**(2026-09 口径) | `video_detail/v1`+`video_model/v1` 轻量 X-Gorgon/X-Khronos(复刻 juku-backend)+随机设备 ID;首选 web `novelquickapp.com/player/{sid}/{vid}` 未加密直链 |

**与本项目实测(六头/轻量 Gorgon+随机设备 → 200 空)的矛盾解释**:缺的可能不是真机,而是——
① **离线 device_register 信封注册**(先拿合法 iid/did 再发请求);
② **公开游客 query 画像**(精确 device_type/resolution/dpi 组合,73532 口径,甚至**无签名**);
③ **gzip body + `x-ss-stub: MD5(gzip)大写 + Content-Encoding: gzip`**(播放接口签名必须覆盖 gzip 后的 body,hongguo-gui 实测口径);
④ 播放签名 **Argus/Ladon 必需、可去 X-Medusa**(HongGuoAPI 口径)。

→ **H3a-2 取流链新增试验矩阵**(不必先等真机):信封注册设备 → 游客画像无签名 → 轻量 Gorgon → 六头(含 x-ss-stub/gzip 变体),逐项对拍。

### 6.2 六头签名生态现状(确认活跃,但口径有版本差)

- **dong/xb(2026-10-07 最终版,TVBox)持续在产**:免费集 SSR 直链 + 锁定集本地 SigServer(127.0.0.1:18888)签名+CENC 流式解密( Range 兼容,内嵌 DEX 签名器)。
- **dong/fun(2026-11)**:SM3 系六头纯算法(~3000 行)仍在维护,口径 = x-ladon/x-argus/x-gorgon/x-helios/x-medusa/x-khronos + x-ss-stub。
- **fanqie-dl(Rust)警示**:**番茄小说 v7.1.3.32 的 metasec 已换算法**——SM3→SHA-256、Simon→AES-128-ECB;同家族不同 app/版本算法不同。**红果(dong/fun=SM3 系)移植前必须用黄金向量锁定本 app 版本的真口径,不能假设与番茄通用**。
- hongguo-gui 的 metasec 版本为 v4.10.3(JNI `ms.bd.c.m.a(IIJ...)` @so+0x297890),与 zhangbaio 2026-06 的 0x27d288 版本不同——**so 版本漂移是常态,离线纯算法方案要跟版本**。

### 6.3 web 通道补充实测(dudt/HongGuoFree 2026-10)

- `hongguoduanju.com` 官网:搜索/详情全通;`/api/video/protected/play-info` **仅前 3 集**(第 4 集起 403 `protected_playback_unavailable`);请求体字段是 **`item_id`**(非 vid),`definition` 只支持 720p/480p(1080p 会 400);返回 `main_url`(DASH 分片)+`player_access_key`+`kid`。
- 网页视频是 CENC 加密(encv/sinf),H1 通道口径不变。
- `novelquickapp.com/player` 未加密直链说法(hongguo-dl)与我们之前实测的 301 矛盾,可能需 applink scheme/特定 UA,列为 H3a 附带试验项,不依赖。

### 6.4 对 H2b 的直接参考

dong/xb 红果短剧.py 的 `_StreamSession` 正是 H2b 规划的本地流式解密代理原型:
- Range 对齐:读入原始窗口 → `counter = IV + skip//16`,`padding = skip%16`(块内偏移修正)→ 解密后截去 padding;
- moov 区用缓存明文替换(moov_plain),非 moov 区逐样本解密;
- HTTP 206/416/Content-Range 全套口径与我们规划一致。**移植 C++ 时 counter 修正公式直接抄。**

## 7. H2b 落码口径(本地流式解密代理,2026-11-15)

`common/hgstream/hgstream.{h,cpp}` + `tests/hgstream.test.cpp`(手动,需外网)。

### 7.1 机制
| 环节 | 实现 |
|---|---|
| 容器改明 | **同长度标签替换**(不动尺寸 → stco 全有效):`encv→hvc1`(有 hvcC)/`avc1`、`enca→mp4a`、`sinf/schi/tenc/senc→free`;仅当标签前 4 字节是合理 box size 时替换(防误伤 payload) |
| Range | httplib `Server::set_content_provider(total, type, provider)` 自带 206/416/Content-Range;provider 收到 `(offset,length,sink)` |
| 解密 | 取窗口密文 → 完全落在窗口内的样本就地解;跨窗口样本单独拉整样本解密后切片(每样本独立 IV=`baseIv+idx`,天然一致) |
| moov 覆盖 | 请求区间与 moov 相交处用改写后字节覆盖(尺寸相同,直接 memcpy) |
| 远端 | handler 内同步 `httplib::Client(scheme://host:port)`(**Client(host,port) 恒明文,TLS 必须 scheme 形式**) |

### 7.2 生命周期(防泄漏)
- **线程**:listen + watchdog 两个 `std::thread`,**绝不 detach**;`stop()` 先置 `running=false`(否则 watchdog 的 `while(running)` 永不退出 → join 挂死)、`srv->stop()`、再逐个 `join()`;
- **会话引用计数**:`open()` refs+1;provider 切集/析构 `release()`;`refs==0 && active==0 && 静默>sessionIdleSec` → 回收;上限 `maxSessions`(LRU 淘汰无引用会话);
- **空闲关**:最后请求超过 `idleSec` 且无在途响应 → watchdog 自动 stop(`reason=idle`),下次 `open()` 重新拉起(端口可能变 → 上层每次 resolve 取最新 URL);
- **日志**:start(port/bind)、stop(reason/sessions/uptime)、threads joined、session open/release/evict、GET offset —— 全走 `DBG_P("hongguo-stream")`;
- 静态析构兜底 `stop()`;所有路径幂等。

### 7.3 环境变量
| 变量 | 含义 |
|---|---|
| `QXVOTUBE_HG_STREAM=off` | 关闭本地代理(resolve 直接走 web 360p) |
| `QXVOTUBE_HG_STREAM_PORT` | 固定端口(默认随机) |
| `QXVOTUBE_HG_IDLE_SEC` | 空闲自动关秒数(默认 600) |
| `QXVOTUBE_HG_SESS_TTL_SEC` | 会话无引用回收秒数(默认 120) |
| `QXVOTUBE_HG_MAX_SESSIONS` | 会话上限(默认 8) |

### 7.4 踩坑记录
1. **`sink.done()` 不能自己调**:httplib 的 DataSink 在 server 内部才绑定 `done`,provider 里调 → `std::bad_function_call`(直接 abort)。正确做法:写数据 + `return true`。
2. **测试 TU 直接 include `httplib.h` 必须带同一份 `CPPHTTPLIB_OPENSSL_SUPPORT/CPPHTTPLIB_ZLIB_SUPPORT`**:否则与库内实例 ODR 冲突 → `SSLClient::process_socket` 断言 `socket.ssl` 失败。
3. **`reap()` 里 join 前必须先置 `running=false`**:watchdog 循环条件读 running,顺序反了就是退出挂死(实测 pvsmoke 卡 300s)。

### 7.5 provider 集成(resolve 顺序)
1. **App 通道 1080p(bytevc1)**:`hgapp::videoModel` → `hgstream::open` → Stream{url=本地口播, quality="1080P", kind="mp4", extra.localProxy/cenc/source/codec};
2. 失败/只有 bytevc2/代理起不来 → **web 360p 明文保底**(fallback-play-info);
3. 切集时 release 上一集会话;provider 析构 release 当前会话。

## 8. 官网搜索分页(2026-11-15 实测)

- `GET https://hongguoduanju.com/api/search/result?query=<kw>&page=N`(`page` 1 起,每页 10 条)。
- 响应自带分页元信息:`pageNum` / `totalPages` / `hasMore`(JSON bool) / `totalCount`;
  hasMore = `hasMore==true || page < totalPages`。
- 注意:服务端排序会漂移,跨页可出现极少量重复(实测 1/10),UI 侧按 id 去重。
- 验收:`tests/paging hongguo` → page1 10 条 / page2 10 条。

## 9. 清晰度选择(resolveQuality,2026-11-15)

- `resolveQuality(id, "1080p")`:App 通道按 `video_list[].video_meta.definition`
  精确选档(大小写不敏感),bytevc1 → 本地解密代理可播。
- `<=720p` 全为 **bytevc2 私有编码**(标准播放器不可解)→ 返回
  `Err::Unsupported` 并提示改选 1080p;不做静默错档。
- 未指定 quality = `pickBest`(优先 bytevc1 最高档),与原 resolve 行为一致。
- 验收:`tests/quality` → 1080p 得 `1080P/bytevc1`,720p 得 Unsupported。

## 10. 清晰度词汇(2026-11-15 补充)

- 统一词汇 `144/160/240/360/480/540/720/1080`(`common/quality.h`)。
- 红果实测档位:`360p/480p/540p/720p` 全为 **bytevc2 私有编码**(不可播),
  仅 `1080p` 为 bytevc1(HEVC)。因此 ≤720p 请求一律 `Err::Unsupported`
  (文案提示改选 1080p),`160/240` 因不存在该档返回 `Err::NotFound`。
- 低清真支持需自研 bytevc2 解码器,当前不做(见 §9)。

## 11. homeFeed(官网首页热播,2026-11-15 实测)

- `GET https://hongguoduanju.com/` 的 SSR 变量 `window._ROUTER_DATA` 里:
  `loaderData.page.homeSections[]` = 4 个 tab:
  `all`(热播短剧)/`human`(真人)/`comic`(漫剧)/`ai`(AI 剧),每类 **9 条**。
- 条目字段:`series_id` / `series_title` / `series_cover` / `series_intro` /
  `episode_cnt` / `pay_type` / `rank`(`extra.tab` 标记所属 tab)。
- 站点无分页参数:只有第一屏 → `page>1` 返回空 + `hasMore=false`。
- 注:页面里另一份 `window._SSR_DATA` 是空壳(mode=stream),**必须读 `_ROUTER_DATA`**。
- 验收:`tests/homefeed` → 36 条(4×9)。

## 12. detail(2026-11-15 复核)

- `_ROUTER_DATA.loaderData.detail_page.seriesDetail`:
  `series_id/series_name/series_cover/series_intro/episode_cnt/pay_type/
   accessible_episode_cnt/vid_list[]/tags[]`。
- `parts[]` = `vid_list` 全集,`part.id = vid`,`part.extra.seriesId` = 剧 id,
  `part.extra.free` = 是否在试看范围内(accessible_episode_cnt)。
- 验收:`tests/detail` → 100 集,seriesId 全一致。

## 13. 抖音 / TikTok 侦察结论(2026-11-15,未落码)

### 抖音(直连可达)
| 口径 | 实测 |
|---|---|
| 热门搜索 `iesdouyin.com/web/api/v2/hotsearch/billboard/word/` | ✅ 免签,`word_list` 48 条(只有词+封面) |
| 热榜 `douyin.com/aweme/v1/web/hot/search/list/` | ✅ 免签,`data.word_list` 48 条(`group_id/word/hot_value`) |
| 分享页 `iesdouyin.com/share/video/<id>/` SSR | ⚠️ `_ROUTER_DATA` 里只有 `itemId/ua/commonContext`,**无 `play_addr`**(口径已变) |
| 视频详情 `douyin.com/aweme/v1/web/aweme/detail/` | ❌ `Blocked by ArgusSecurityPlugin Uifid Not Found`(需 uifid cookie/Argus) |
| web 搜索 `/aweme/v1/web/general/search/single/` | ❌ HTTP 200 + `data:[]`(需 a_bogus) |
| 旧 `iesdouyin.com/web/api/v2/aweme/iteminfo/` | ❌ 0 字节(已下线) |

→ **可免签的只有"话题榜/热词",拿不到可播地址**;要 `search/detail/resolve`
必须 a_bogus(web)或 App 签名栈(X-Gorgon/X-Argus,与红果同源)。

### TikTok
| 口径 | 实测 |
|---|---|
| 代理 `127.0.0.1:10809` 访问 | ❌ CONNECT 成功但 TLS 无响应(timeout,经代理不可达) |
| `oembed` / `/api/post/item_list/` | ❌ 0 字节(同上,且需 X-Bogus/msToken) |

→ 前置条件:① 可用的境外代理 ② X-Bogus/msToken 签名。当前环境两条都不满足。

### 结论
- 抖音/TikTok **不落 provider 代码**;标 P2-blocked(需签名栈 + 可达代理)。
- 签名栈可复用红果的 bytedance 设备注册骨架(`common/hgapp`),
  但抖音/ TikTok 与红果不同:**没有零签名等价面**,必须实现 a_bogus/X-Bogus
  或 App 六头(参考 dong/fun 2026-11 口径),并与版本强绑定维护。
- 若后续要做:建议先只做"抖音热榜 provider"(免签、有真实内容),
  search/resolve 等签名栈成熟再补。

### 13.1 已落码:douyin provider(仅热榜)
- `providers/douyin/douyinprovider.{h,cpp}`:id=`douyin`;
  `homeFeed(page)` → 热榜 49 条(免签),`page>1` 返回空+`hasMore=false`;
  `search/detail/resolve` → 显式 `Err::Unsupported`(文案指向本文档),
  不返回假数据。
- 条目口径:`id="hot:<group_id>"`、`title=word`、`extra`:
  `hotValue/videoCount/discussCount/rank/kind=hotword/cover`。
- 验收:`tests/homefeed douyin` → 49 条;`pvsmoke douyin` → `[skip]`(不是失败)。

### 7.6 分层重构(2026-11-15 Wave C)

- `common/localstream/`:通用本地流服务(HTTP Range + 会话引用计数 + 空闲关 +
  线程 join 账本)。只依赖 `Source` 抽象:
  `total()` / `read(off,len,*out)`(出明文)/ `contentType()`。
  对外 URL:`http://127.0.0.1:<port>/s/<token>.mp4`(旧 `/hg/` 已弃用)。
- `common/hgstream/`:降为**适配层**——`hgstream::Server::instance().open()`
  内部构造 `CencSource`(moov 同长度改写 + 样本窗口解密)注册给 localstream;
  公开 API 不变(provider 无需改动),`samplesOf()` 用 `dynamic_pointer_cast`
  取回 CencSource。
- env:新名 `QXVOTUBE_STREAM_IDLE_SEC/_SESS_TTL_SEC/_MAX_SESSIONS/_PORT`,
  旧 `QXVOTUBE_HG_*` 仍兼容(优先新名)。
- 目的:Wave D 的 YouTube DASH 合流只需新增一个 `DashMergeSource`,
  复用同一套服务/回收/空闲关。

## 14. 红果专项收口(2026-11-15)

### 14.1 bytevc2 策略定稿
- App 侧 ≤720p 全是 **bytevc2 私有编码**(标准播放器不可解)。
- `resolveQuality(≤720p)`:**改走官网明文通道**(H1 `fallback-play-info`,360P,
  仅免费集),`Stream.extra.source=web` + `requestedQuality` + 不相符时 `degraded=true`。
- 官网也拿不到(付费墙)→ `Err::Unsupported`("≤720p 仅官网免费集可用…请选 1080p")。
- `1080p` 仍走 App 链 + H2b 本地解密代理。

### 14.2 多集批量预取
- `hgapp::videoModels(vids)`:一次 `mixed_video_id_map` 请求多集(实测 3/3 命中);
- `hgapp` 内 tracks 缓存(10 分钟,`videoModel` 命中即回调);
- `detail` 后按 `QXVOTUBE_HG_PREFETCH=N`(默认 3,0=关)后台预热前 N 集。
- **并发注册坑**:并发 `ensureDevice` 曾各自注册,服务端返回 `device_id=0`
  并覆盖有效设备 → 后续请求 `device_id=0` 返回异常体。修复:
  ① `Device::valid()` 拒绝 `"0"`;② 注册合流(在途时排队,只注册一次);
  ③ 失败不写缓存。

### 14.3 官网榜单分页(homeFeed page>=2)
- 第 1 页:首页 SSR `homeSections`(4 tab × 9 = 36 条)。
- 第 2 页起:`GET /rank/hot-drama?page=N`(20 条/页,
  `loaderData["rank_hot-drama/page"].content.rankList[]`,字段
  `seriesId/title/cover/rank/heatText/scoreText`),`pagination.totalPages`
  给 hasMore。
- 首页 hasMore 现为 `true`;跨页与首页有重叠(首页是每类 top9),UI 按 id 去重。
- 验收:`tests/homefeed hongguo` → p1 36 / p2 20(dup=9,容忍)。
## 错误映射(2026-11-15)

- 官网 `403/400 public_fallback_unavailable` → `Paywalled`
  (detail: `http.status`, `server.code=public_fallback_unavailable`, `server.message`)
- App 链 `code!=0` → `Parse`(detail: `server.code`, `server.message`)
- douyin 未实现能力 → `Unsupported`(附 `auth.mode`);热榜 `status_code!=0` → `Parse`
