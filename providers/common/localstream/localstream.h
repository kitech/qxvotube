#pragma once
#include <stdint.h>
#include "common/compat.h"
#include "common/result.h"

#include <functional>
#include <memory>

// 通用本地流式媒体服务(HTTP Range + 会话引用计数 + 空闲自动关)。
//
// 设计:与具体解密/合流逻辑解耦 —— 调用方注册一个 Source(按需产出明文分片),
// 播放器连 http://127.0.0.1:<port>/s/<token>.mp4 即可边拉边播。
// 现有实现:红果 CENC 解密源(hgstream::CencSource);Wave D 将加 DASH 合流源。
//
// 生命周期(防泄漏):
//   - listen + watchdog 两个 std::thread,绝不 detach;stop() 先置 running=false
//     再 srv.stop() 再逐个 join(顺序反了会挂死);
//   - 会话引用计数:open() refs+1;调用方 release();refs==0 && active==0 &&
//     静默 > sessionIdleSec → 回收;上限 maxSessions(LRU 淘汰无引用会话);
//   - 看门狗:最后请求超过 idleSec → 自动 stop,下次 open 重新拉起;
//   - 静态析构兜底 stop();所有路径幂等。
namespace qxv {
namespace localstream {

// 明文数据源:实现方自己决定怎么取值(本地文件/远端解密/合流)
class Source {
public:
    virtual ~Source() {}
    virtual uint64_t total() const = 0;
    // 读 [off, off+len) 的明文;失败返回 false。可返回少于 len(EOF)。
    virtual bool read(uint64_t off, uint64_t len, QByteArray* out) = 0;
    virtual QString contentType() const { return qFromUtf8("video/mp4"); }
};

// 内存源:整块字节直接可播(用于改写后的 HLS 播放列表等)
class BufferSource : public Source {
public:
    BufferSource(const QByteArray& data,
                 const QString& contentType = qFromUtf8("video/mp4"))
        : data_(data), contentType_(contentType) {}
    uint64_t total() const override { return uint64_t(data_.size()); }
    bool read(uint64_t off, uint64_t len, QByteArray* out) override {
        if (off >= uint64_t(data_.size())) { *out = QByteArray(); return true; }
        const uint64_t left = uint64_t(data_.size()) - off;
        const uint64_t n = len < left ? len : left;
        *out = qbaMid(data_, int(off), int(n));
        return true;
    }
    QString contentType() const override { return contentType_; }

private:
    QByteArray data_;
    QString contentType_;
};

struct Options {
    int idleSec = 600;          // 无请求多久自动关服务(0=不自动关)
    int sessionIdleSec = 120;   // 会话无引用无流量多久回收
    int maxSessions = 8;        // LRU 上限
    int fixedPort = 0;          // 0=随机端口
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

    void configure(const Options& o);      // 运行中不生效(避免竞态)
    Options options() const;

    Result start();                        // 幂等
    void stop(const QString& reason = qFromUtf8("shutdown"));   // 幂等
    int port() const;
    Stats stats() const;

    // 注册一个源(key 由调用方定义,用于 release/tokenOf);refs+1。
    // 返回可播 URL(http://127.0.0.1:<port>/s/<token>.mp4)。
    Result open(const QString& key, std::shared_ptr<Source> src, QString* outUrl);

    void release(const QString& key);

    QString tokenOf(const QString& key) const;
    std::shared_ptr<Source> sourceOf(const QString& token) const;

private:
    Server();
    ~Server();
    Server(const Server&);
    Server& operator=(const Server&);
    struct Impl;
    std::shared_ptr<Impl> d;
};

} // namespace localstream
} // namespace qxv
