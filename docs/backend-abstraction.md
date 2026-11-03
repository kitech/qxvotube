# qlvotube 后端统一封装设计

## 设计原则

1. **复用现网基础设施**:HTTP 全部走 qlappcom `EventPoller`(curl 泵线程,回调经 `QApplication::postEvent` 投回 UI 线程,即 `api.cpp` 的 `ApiHttpResultEvent` 模式)。适配器不另起线程。
2. **JSON 全部用 cJSON/hjson 解析**(现有依赖,不引入新库)。
3. **接口为纯虚、异步回调经事件回投**:UI 层仅消费 VideoItem,从不感知各后端差异。
4. **Qt3 兼容**:QString 方集、容器用 std::vector、无 chrono/lambda(QT3_BUILD 分支照 compat34.h 约定)。

## 数据模型(`src/videomodel.h`)

```cpp
struct VideoItem {
    std::string backendId;    // "youtube"/"peertube"/"libretv"/"bilibili"
    std::string backendInst;  // 来源(实例/站点 url,用于跨数组还原)
    std::string videoId;      // 后端内主键: YT videoId / PT uuid / CMS vod_id / B站 bvid
    std::string aid;          // B 站可选: avid;其他后端留空
    std::string cid;          // B 站分P id(其他后端空)
    QString title;
    QString channel;          // 作者/UP主/频道名
    std::string channelId;    // 频道键(可跳频道列表)
    int durationSec;          // 0 = 未知/直播中
    int64_t views;
    QString thumbUrl;         // 缩略图(缺省时 UI 用 identicon 兜底)
    QString url;              // 已解析直链(空 = 需 resolve)
    bool live;                // 直播中(Piper/PT isLive、B站室, CMS 未知为 false)
    // 采集站剧集概念: LibreTV/CMS 源一影片多集
    int episode;              // 0 = 单集
    int episodeCount;
    std::string displayUrlMethod; // 内部: "hls"/"mp4"/"dash"/"proxy"
};
```

```cpp
struct ListingPage {
    std::vector<VideoItem> items;
    std::string nextPageToken;  // 各后端自封装: Piped "nextpage" 原串 / PT startIndex / B站页码 pn / CMS pg / YT "pageToken"
    bool hasMore;
    QString error;              // 非空 = 失败(风控/超时/解析错)
};
```

```cpp
struct StreamInfo {             // resolve() 结果
    std::string kind;           // "mp4" | "hls" | "dash" | "separate"(视频/音频分轨,如 B 站)
    std::string videoUrl;
    std::string audioUrl;       // kind=separate 时用(mpv --audio-file 合流)
    int quality;                // 清晰度序号(如 B 站 qn、PT resolution id);0=取最高
    QStringList httpHeaders;    // 引擎需附加: B 站 "Referer: https://www.bilibili.com/"、UA
    int expireSec;              // URL 有效期(B 站 7200 秒),0=不限
    QString title; QString subtitleText; // 可选: 字幕轨信息(首版不消费)
};
```

## 抽象接口(`src/videobackend.h`)

```cpp
enum BackendOp {
    OpListTrending,       // 首页: 有"热门/推荐"
    OpSearch,             // 关键词搜索(带 filter)
    OpChannelVideos,      // 频道/UP主空间/采集站内详情列表
    OpVideoDetail,        // 详情+resolve 播放地址
    OpResolveStream,      // 单独解析播放地址(详情后播前调用)
    OpSuggestions,        // 搜索联想(Piped /suggestions、B站热词;可选)
};

struct BackendInfo {
    const char* id;          // videobackend key
    const char* titleKey;    // lang 键: "backends.youtube" ...
    bool needsAuth;
    bool hasInternalConfig;  // 需要配置(实例 URL/key/源列表)
};

class VideoBackend {
public:
    virtual ~VideoBackend() {}
    virtual BackendInfo info() const = 0;

    // 异步:完成后 postEvent(BackendResultEvent(op,呼入target,user))
    virtual void listTrending(const QString& region, void* user) = 0;
    virtual void search(const QString& kw, const QString& filter, void* user) = 0;
    virtual void channelVideos(const std::string& channelId, void* user) = 0;
    virtual void videoDetail(const VideoItem& v, void* user) = 0;
    virtual void resolveStream(const VideoItem& v, int preferredQuality, void* user) = 0;

    virtual void nextPage(const std::string& token, void* user) = 0;  // 上一结果页 nextPageToken
    
    virtual bool configure(const QVariantMap& cfg) = 0; // 实例 URL/key/源列表)
};
```

- `BackendRegistry`(`videobackend.cpp`):单例,按 `BackendInfo.id` 注册四个适配器实例;`current()` 由 UI 源切换驱动。
- **事件回投**:新 `BackendResultEvent : ApiResultEvent`(复用 ApiResultReadyType + type=BackendOp),携带 `ListingPage/StreamInfo/void* user`,UI 在 `customEvent` 分发(现 MainMainWindow 已习惯此模式)。

## 4 个适配器的共性区分

| 能力 | Piped | YouTube官方v3 | PeerTube | CMS(LibreTV) | Bilibili |
|---|---|---|---|---|---|
| 首页 trending | ✓(region) | ✓ chart=mostPopular | ×(各实例不提供;显示实例最新 videos) | ×(源无首页;显示"多源并行待搜") | ✓ popular |
| search | ✓ filter | ✓(100 quota/次) | ✓ SepiaSearch | ✓ wd(多源并行) | ✓ all/v2(WBI) |
| 频道视频 | ✓ nextpage/channel | ✓ channels.contentDetails | ✓ video-channels | ×(以影片详情替换) | ×(用户空间 wbi/x/space) |
| 播放 resolve | ✓ /streams | ×(无直链,需 ytdl 或 Piped 兜底) | ✓ fileUrl/HLS 直连 | ✓ 详情 JSON 现成 | ✓ playurl(WBI+cookie) |
| 鉴权 | ×(自建实例为 auth 需) | API key | 可选 token | × | wbi + cookie(可选/必选分情况) |

## PlayerEngine(`src/playerengine.h/.cpp`)

```cpp
class PlayerEngine {
public:
    virtual bool open(const StreamInfo& s, const QString& title) = 0; // 提回窗口句柄
    virtual void play(); virtual void pause(); virtual void stop();
    virtual void setVolume(int pct); virtual void setPosition(double sec);
    virtual double position() const; virtual double duration() const;
    virtual void setRate(double r);
    virtual bool present() const; // 句柄是否有效
};
```

### 引擎实现矩阵

| 引擎 | 实现 | 平台 | 编译 | 降级顺序 |
|---|---|---|---|---|
| ProcessMpv | QProcess 拉起 `mpv`(Linux `--wid=X11 winId`;Windows `--wid=HWND`;macOS 无必依赖 --wid,浮独立窗) | 全平台(需系统装 mpv;win 提供 portable 获取指引) | 恒编 | 1 |
| LibMpv | 链接 libmpv OpenGL 渲染控件(qlmplayeradapter: Qt3 X11 用 XRENDER 纯 Qt/egl 已足、Qt6 自绘窗口) | linux/mac/win | 条件宏 `WITH_LIBMPV`(CI 默认关) | 2 |
| QMediaPlayer | Qt6 only(win/mac 原生后端零额外依赖;linux 需 dev) | Qt6 build | 条件宏 `WITH_QTMEDIA` | 3 |

- 引擎选择:Config `videoengine` = "auto"|"processmpv"|"libmpv"|"qtmplayer";auto 按上述顺序探测(mpv 可执行文件在 PATH→present)。
- StreamInfo 头映射到引擎参数:`httpHeaders` → mpv `--http-header-fields=`;`kind=separate` → 视频主 URL+`--audio-file=`;HLS/DASH 容器 mpv 原生;CMS m3u8 广告分片过滤按 host 黑名单(首版不做)。
- B 站 Referer/UA 必传(引擎级统一注入)。

## HTTP 请求规范(适配器内约定)

- per-request 超时:列表类 8 秒,resolve 类 15 秒(对齐 LibreTV 服务端聚合池)。
- UA 统一:`qlvotube/<version> (+backend-specific UA 差异处理: B站用真实浏览器 UA)`。
- 重试:网络层失败重试 1 次;5xx/超时→实例/源 fallback(仅 Piped/多 CMS 源)。
- 缩略图:image url → EventPoller `ApiMediaDownload` → cache_fs 存盘(文件名 sha1(url));UI 命中本地直接读。

## 事件时序(以搜索为例)

1. UI: `BackendRegistry::current()->search("关键词","", user=listener)`
2. 适配器组 HttpRequest(QS utF escape)→ `EventPoller::addRequest`
3. 泵线程 curl 完成 → `onHttpDone` → `QApplication::postEvent(..., BackendResultEvent)`
4. UI `customEvent` → ListingPage 过滤 → ContentView 刷新
5. 若 `nextPageToken.hasGi`,上滑触底时调 `nextPage()`

## 目录/文件规划

```
src/
  videomodel.h          // 纯数据模型+helper(qDurationStr 等格式化)
  videobackend.h/.cpp   // 接口 + BackendRegistry + BackendResultEvent
  backend_piped.{h,cpp}
  backend_ytv3.{h,cpp}  // 官方 API v3(可编译开关,未配 key 时不实例化)
  backend_peertube.{h,cpp}
  backend_cms.{h,cpp}   // LibreTV/Apple CMS
  backend_bili.{h,cpp}  // 内含 WbiSigner/QrPoll
  playerengine.{h,cpp}  // 接口 + 探测/工厂
  engine_mpvproc.{h,cpp}
  engine_libmpv.{h,cpp} // WITH_LIBMPV
  engine_qtmedia.{h,cpp}// WITH_QTMEDIA(Qt6)
```
