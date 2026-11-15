#pragma once
#include "common/compat.h"
#include "common/result.h"

#include <functional>

// 红果 CENC 密文的本地流式解密代理(2026-11-15 实测定案):
//   player 连 http://127.0.0.1:<port>/hg/<token>.mp4 → 明文渐进 mp4(Range/seek 兼容)
//
// 机制:
//   ① open() 拉远端头部 → 定位 moov → 同长度标签改写(encv→hvc1/avc1、enca→mp4a、
//      sinf/schi/tenc/senc→free;不动尺寸 → stco 偏移全部有效) → 缓存;
//   ② HTTP 请求按 Range 取 [off,len) → 远端拉该窗口密文 → 按 hgcenc 样本索引
//      逐样本 AES-128-CTR 解密(IV=baseIv+idx) → moov 区用改写后字节覆盖;
//   ③ 跨窗口样本单独拉整样本解密后切片(CTR 逐样本独立,天然一致)。
//
// 生命周期(防泄漏):
//   - listen 线程 + watchdog 线程全部 std::thread 持有,绝不 detach,stop() 逐个 join;
//   - 会话引用计数:open() refs+1,provider 切集/析构 release();refs==0 且无在途响应
//     且静默超过 sessionIdleSec → 回收;
//   - 看门狗:最后请求超过 idleSec → 自动 stop(下次 open 重新拉起);
//   - 静态析构兜底 stop();所有路径幂等。
namespace qxv {
namespace hgstream {

struct Options {
    int idleSec = 600;          // 无请求多久自动关服务(0=不自动关)
    int sessionIdleSec = 120;   // 会话无引用无流量多久回收
    int maxSessions = 8;        // LRU 上限
    int fixedPort = 0;          // 0=随机端口
    int prefetchBytes = 2 << 20;// open 时先拉的头长度(定位 moov)
};

struct Stats {
    bool running = false;
    int port = 0;
    int sessions = 0;
    int activeStreams = 0;
    QString lastStopReason;
    qint64 uptimeMs = 0;
};

class Server {
public:
    static Server& instance();

    // 配置(测试注入短超时;也可由 env 覆盖:
    // QXVOTUBE_HG_STREAM=off / _PORT / _IDLE_SEC / _SESS_TTL_SEC / _MAX_SESSIONS)
    void configure(const Options& o);
    Options options() const;

    Result start();                       // 幂等
    void stop(const QString& reason = qFromUtf8("shutdown"));   // 幂等
    int port() const;
    Stats stats() const;

    // 打开一集的可播地址。remoteUrl+spadeA 来自 hgapp::videoModel;
    // refs+1(调用方负责 release)。失败返回 Err(Parse/Network/Unsupported)。
    Result open(const QString& vid, const QString& quality,
                const QString& remoteUrl, const QString& spadeA,
                QString* outUrl);

    void release(const QString& vid, const QString& quality);

    // 调试/测试:该会话前 n 个视频样本(偏移/大小);失败返回空。
    struct SampleRef { quint64 offset; int size; bool video; };
    QList<SampleRef> samplesOf(const QString& token, int n) const;
    QString tokenOf(const QString& vid, const QString& quality) const;

    void setProxy(const QString& proxy);   // "http://host:port"(CDN 直连一般不需要)

private:
    Server();
    ~Server();
    Server(const Server&);
    Server& operator=(const Server&);
    struct Impl;
    Impl* d;
};

} // namespace hgstream
} // namespace qxv
