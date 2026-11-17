#include <stdint.h>
#include "hgstream.h"

#include "common/debuglog.h"
#include "common/hgcenc/cenc.h"
#include "common/hgcenc/mp4box.h"
#include "common/hgcenc/spade.h"

#include "httplib.h"

#ifdef QT3_BUILD
#ifdef QT3_BUILD
#include <qcstring.h>
#else
#include <QByteArray>
#endif
#else
#include <QByteArray>
#endif
#include <qdatetime.h>
#include <qmap.h>

#include <memory>

// 口径:docs/providers/protocols/bytedance.md §6.1/§7。
// HTTP 服务/会话/线程/缓存全部在 localstream;这里只有 CENC 相关逻辑。

namespace qxv {
namespace hgstream {

namespace {

// Qt3 QByteArray 无 fromHex:本地实现(hex 串 → 字节)
static QByteArray hexToBytes(const QString& hex) {
    const QByteArray h = qToUtf8(hex);
    std::string out;
    for (int i = 0; i + 1 < (int)h.size(); i += 2) {
        const char a = h[i], b = h[i + 1];
        const int hi = (a >= 'a') ? (a - 'a' + 10)
                      : (a >= 'A') ? (a - 'A' + 10) : (a - '0');
        const int lo = (b >= 'a') ? (b - 'a' + 10)
                      : (b >= 'A') ? (b - 'A' + 10) : (b - '0');
        out.push_back((char)((hi << 4) | lo));
    }
    return qbaFromData(out.data(), (int)out.size());
}



const char* kMod = "hongguo-stream";
const char* kUa = "com.phoenix.read/73532 (Linux; U; Android 16; zh_CN)";

uint32_t rd32(const QByteArray& b, uint64_t o) {
    return (uint32_t(static_cast<unsigned char>(b[int(o)])) << 24)
         | (uint32_t(static_cast<unsigned char>(b[int(o) + 1])) << 16)
         | (uint32_t(static_cast<unsigned char>(b[int(o) + 2])) << 8)
         |  uint32_t(static_cast<unsigned char>(b[int(o) + 3]));
}

uint64_t rd64(const QByteArray& b, uint64_t o) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | uint32_t(static_cast<unsigned char>(b[int(o) + i]));
    return v;
}

struct Fetched {
    bool ok = false;
    int status = 0;
    QByteArray data;
    uint64_t total = 0;
    QString err;
};

// 远端 Range GET(带 UA/跟随跳转);红果 CDN 直连,代理仅在显式设置时走
Fetched fetchRange(const QString& url, uint64_t off, uint64_t len,
                   const QString& proxy) {
    Fetched f;
    const int sp = qIndexOf(url, qFromUtf8("://"));
    if (sp < 0) { f.err = qFromUtf8("bad url"); return f; }
    const QString scheme = url.left(sp);
    const int slash = qIndexOf(url, qFromUtf8("/"), sp + 3);
    const QString hostPort = slash < 0 ? url.mid(sp + 3)
                                       : url.mid(sp + 3, slash - sp - 3);
    const QString path = slash < 0 ? QString() : url.mid(slash);
    const int colon = qLastIndexOf(hostPort, qFromUtf8(":"));
    const QString host = colon < 0 ? hostPort : hostPort.left(colon);
    const int port = colon < 0 ? (scheme == qFromUtf8("https") ? 443 : 80)
                              : hostPort.mid(colon + 1).toInt();
    try {
        httplib::Client cli(std::string(
            qToUtf8(qFromUtf8("%1://%2:%3").arg(scheme, host).arg(port)).data()));
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(30, 0);
        cli.set_write_timeout(10, 0);
        if (!proxy.isEmpty()) {
            const int ps = qIndexOf(proxy, qFromUtf8("://"));
            const QString hp = ps >= 0 ? proxy.mid(ps + 3) : proxy;
            const int c = qLastIndexOf(hp, qFromUtf8(":"));
            cli.set_proxy(std::string(qToUtf8(hp.left(c)).data()), hp.mid(c + 1).toInt());
        }
        httplib::Headers h;
        const std::string range =
            qToUtf8(qFromUtf8("bytes=%1-%2").arg(off).arg(off + len - 1)).data();
        h.emplace("Range", range);
        h.emplace("User-Agent", std::string(kUa));
        auto r = cli.Get(std::string(qToUtf8(path).data()), h);
        if (!r) { f.err = qFromUtf8("connect failed"); return f; }
        f.status = r->status;
        f.data = qbaFromData(r->body.data(), int(r->body.size()));
        if (r->status == 206) {
            const QString cr = QString::fromUtf8(
                r->get_header_value("Content-Range").c_str());
            const int sl = qLastIndexOf(cr, qFromUtf8("/"));
            if (sl >= 0) f.total = cr.mid(sl + 1).toULongLong();
            f.ok = true;
        } else if (r->status == 200) {
            f.ok = off == 0;
            f.total = uint64_t(f.data.size());
        } else {
            f.err = qFromUtf8("http %1").arg(r->status);
        }
    } catch (...) {
        f.err = qFromUtf8("exception");
    }
    return f;
}

// 同长度标签替换(仅当标签前 4 字节是合理 box size;不动尺寸 → stco 全有效)
bool renameTag(QByteArray& moov, const char* from, const char* to) {
    const QByteArray pat = qbaFromData(from, 4);
    int hits = 0;
    int i = int(qIndexOf(moov, pat, 0));
    while (i >= 8) {
        const uint32_t sz = rd32(moov, uint64_t(i) - 4);
        if (sz >= 8 && uint64_t(sz) <= uint64_t(moov.size())) {
            memcpy(moov.data() + i, to, 4);   // Qt3 QByteArray 无 replace
            ++hits;
            i = int(qIndexOf(moov, pat, i + 4));
        } else {
            i = int(qIndexOf(moov, pat, i + 1));
        }
    }
    return hits > 0;
}

void patchMoov(QByteArray& moov) {
    const bool hevc = qIndexOf(moov, qbaFromData("hvcC", 4)) >= 0;
    renameTag(moov, "encv", hevc ? "hvc1" : "avc1");
    renameTag(moov, "enca", "mp4a");
    renameTag(moov, "sinf", "free");
    renameTag(moov, "schi", "free");
    renameTag(moov, "tenc", "free");
    renameTag(moov, "senc", "free");
}

// ── CENC 源:按 Range 拉密文窗口 → 逐样本解密 ──────────────────────
class CencSource : public localstream::Source {
public:
    QString vid;
    QString quality;
    QString remoteUrl;
    QString proxy;
    QByteArray key;                 // 16B
    uint64_t total_ = 0;
    uint64_t moovOff = 0;
    QByteArray moovPatched;
    QList36<hgcenc::TrackInfo> tracks;

    uint64_t total() const override { return total_; }

    bool read(uint64_t off, uint64_t len, QByteArray* out) override {
        Fetched f = fetchRange(remoteUrl, off, len, proxy);
        if (!f.ok) {
            DBG_P(kMod, Warn, "read failed off=%llu len=%llu: %s",
                  (unsigned long long)off, (unsigned long long)len,
                  qToUtf8(f.err).data());
            return false;
        }
        *out = f.data;
        const uint64_t outEnd = off + uint64_t(out->size());

        // moov 区:改写后的字节直接覆盖(尺寸相同)
        const uint64_t moovEnd = moovOff + uint64_t(moovPatched.size());
        if (moovOff < outEnd && moovEnd > off) {
            const uint64_t a = off > moovOff ? off : moovOff;
            const uint64_t b = outEnd < moovEnd ? outEnd : moovEnd;
            memcpy(out->data() + int(a - off),
                   moovPatched.data() + int(a - moovOff), size_t(b - a));
        }

        // 样本区:窗口内就地解;跨窗口样本拉整样本解密后切片
        for (int ti = 0; ti < tracks.size(); ++ti) {
            const hgcenc::TrackInfo& t = tracks[ti];
            for (int i = 0; i < t.sampleOffsets.size(); ++i) {
                const uint64_t so = t.sampleOffsets[i];
                const int sz = int(t.sampleSizes[i]);
                if (so >= outEnd) break;
                if (so + uint64_t(sz) <= off) continue;
                if (so >= off && so + uint64_t(sz) <= outEnd) {
                    hgcenc::TrackInfo one = t;
                    one.sampleOffsets = QList36<uint64_t>() << (so - off);
                    one.sampleSizes = QList36<uint32_t>() << uint32_t(sz);
                    one.baseIv = t.baseIv + uint64_t(i);
                    if (hgcenc::Cenc::decryptTrack(*out, one, key) != 1)
                        return false;
                } else {
                    Fetched sf = fetchRange(remoteUrl, so, uint64_t(sz), proxy);
                    if (!sf.ok || sf.data.size() != sz) return false;
                    QByteArray one = sf.data;
                    hgcenc::TrackInfo tt = t;
                    tt.sampleOffsets = QList36<uint64_t>() << 0;
                    tt.sampleSizes = QList36<uint32_t>() << uint32_t(sz);
                    tt.baseIv = t.baseIv + uint64_t(i);
                    if (hgcenc::Cenc::decryptTrack(one, tt, key) != 1)
                        return false;
                    const uint64_t a = off > so ? off : so;
                    const uint64_t soEnd = so + uint64_t(sz);
                    const uint64_t b = outEnd < soEnd ? outEnd : soEnd;
                    memcpy(out->data() + int(a - off),
                           one.data() + int(a - so), size_t(b - a));
                }
            }
        }
        return true;
    }
};

QString sessionKey(const QString& vid, const QString& quality) {
    return vid + qFromUtf8("|") + quality;
}

} // namespace

struct Server::Impl {
    QString proxy;
};

Server::Server() : d(new Impl) {}
Server::~Server() { delete d; }

Server& Server::instance() {
    static Server s;
    return s;
}

void Server::configure(const Options& o) {
    localstream::Server::instance().configure(o);
}

Options Server::options() const {
    return localstream::Server::instance().options();
}

Result Server::start() { return localstream::Server::instance().start(); }

void Server::stop(const QString& reason) {
    localstream::Server::instance().stop(reason);
}

int Server::port() const { return localstream::Server::instance().port(); }

Stats Server::stats() const { return localstream::Server::instance().stats(); }

void Server::setProxy(const QString& proxy) { d->proxy = proxy; }

Result Server::open(const QString& vid, const QString& quality,
                    const QString& remoteUrl, const QString& spadeA,
                    QString* outUrl) {
    const QByteArray key = hexToBytes(
        hgcenc::Spade::unwrapV1(
            base64Decode(std::string(qToUtf8(spadeA).data())), 0));
    if (key.size() != 16)
        return Result::fail(Err::Parse, qFromUtf8("hgstream: spade unwrap 失败"));

    // 头部:定位 moov(不足则按精确 Range 补拉)
    Fetched head = fetchRange(remoteUrl, 0, 2u << 20, d->proxy);
    if (!head.ok)
        return Result::fail(Err::Network,
                            qFromUtf8("hgstream: 头部拉取失败 %1").arg(head.err));
    uint64_t total = head.total ? head.total : uint64_t(head.data.size());

    uint64_t moovOff = 0, moovSize = 0;
    {
        uint64_t o = 0;
        while (o + 8 <= uint64_t(head.data.size())) {
            const uint64_t sz = rd32(head.data, o);
            const uint32_t ty = rd32(head.data, o + 4);
            if (ty == 0x6d6f6f76) {   // 'moov'
                moovOff = o;
                moovSize = (sz == 1) ? rd64(head.data, o + 8) : sz;
                break;
            }
            if (sz < 8) break;
            o += sz;
        }
    }
    QByteArray moov;
    if (moovSize == 0)
        return Result::fail(Err::Parse, qFromUtf8("hgstream: 无 moov"));
    if (moovOff + moovSize <= uint64_t(head.data.size())) {
        moov = qbaMid(head.data, int(moovOff), int(moovSize));
    } else {
        Fetched mf = fetchRange(remoteUrl, moovOff, moovSize, d->proxy);
        if (!mf.ok || uint64_t(mf.data.size()) < moovSize)
            return Result::fail(Err::Network, qFromUtf8("hgstream: moov 拉取失败"));
        moov = qbaLeft(mf.data, int(moovSize));
    }

    QList36<hgcenc::TrackInfo> tracks;
    if (hgcenc::findTracks(moov, tracks) != 0)
        return Result::fail(Err::Parse, qFromUtf8("hgstream: 加密轨解析失败"));
    patchMoov(moov);

    auto src = std::make_shared<CencSource>();
    src->vid = vid;
    src->quality = quality;
    src->remoteUrl = remoteUrl;
    src->proxy = d->proxy;
    src->key = key;
    src->total_ = total;
    src->moovOff = moovOff;
    src->moovPatched = moov;
    src->tracks = tracks;

    return localstream::Server::instance().open(sessionKey(vid, quality), src,
                                                outUrl);
}

void Server::release(const QString& vid, const QString& quality) {
    localstream::Server::instance().release(sessionKey(vid, quality));
}

QList36<SampleRef> Server::samplesOf(const QString& token, int n) const {
    QList36<SampleRef> out;
    auto base = localstream::Server::instance().sourceOf(token);
    auto src = std::dynamic_pointer_cast<CencSource>(base);
    if (!src) return out;
    for (int ti = 0; ti < src->tracks.size(); ++ti) {
        const hgcenc::TrackInfo& t = src->tracks[ti];
        for (int i = 0; i < t.sampleOffsets.size() && out.size() < n; ++i)
            out.append(SampleRef{ t.sampleOffsets[i], int(t.sampleSizes[i]),
                                  t.isVideo });
    }
    return out;
}

QString Server::tokenOf(const QString& vid, const QString& quality) const {
    return localstream::Server::instance().tokenOf(sessionKey(vid, quality));
}

} // namespace hgstream
} // namespace qxv
