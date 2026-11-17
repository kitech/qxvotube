#pragma once
#include "common/compat.h"
#include "common/localstream/localstream.h"
#include "common/result.h"

#include <qlist.h>
#include <qstring.h>

// 红果 CENC 密文 → 本地明文流(hgstream)。HTTP/会话/生命周期全部由
// common/localstream 承担;本层只做:
//   ① 拉远端头部定位 moov → 同长度标签改写(encv→hvc1、sinf/senc→free);
//   ② 提供 CencSource:按 Range 拉窗口密文 → 逐样本 AES-128-CTR 解密 → 明文。
// 播放器连 http://127.0.0.1:<port>/s/<token>.mp4 即可播(seek/Range 兼容)。
//
// 说明:URL 前缀为 /s/(通用路径);QXVOTUBE_HG_* 环境变量仍兼容,
// 新变量名见 localstream.h。
namespace qxv {
namespace hgstream {

using Options = localstream::Options;
using Stats = localstream::Stats;

struct SampleRef {
    quint64 offset;
    int size;
    bool video;
};

class Server {
public:
    static Server& instance();

    void configure(const Options& o);
    Options options() const;

    Result start();
    void stop(const QString& reason = qFromUtf8("shutdown"));
    int port() const;
    Stats stats() const;

    // remoteUrl + spadeA 来自 hgapp::videoModel;refs+1(调用方 release)
    Result open(const QString& vid, const QString& quality,
                const QString& remoteUrl, const QString& spadeA,
                QString* outUrl);
    void release(const QString& vid, const QString& quality);

    // 调试/测试:该会话前 n 个样本(偏移/大小,是否视频轨)
    QList36<SampleRef> samplesOf(const QString& token, int n) const;
    QString tokenOf(const QString& vid, const QString& quality) const;

    void setProxy(const QString& proxy);

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
