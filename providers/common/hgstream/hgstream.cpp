#include "hgstream.h"

#include "common/debuglog.h"
#include "common/hgcenc/cenc.h"
#include "common/hgcenc/mp4box.h"
#include "common/hgcenc/spade.h"

#include "httplib.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>

#include <memory>

// 口径:docs/providers/protocols/bytedance.md §6.1/§7。
// HTTP 服务/会话/线程/缓存全部在 localstream;这里只有 CENC 相关逻辑。

namespace qxv {
namespace hgstream {

namespace {

const char* kMod = "hongguo-stream";
const char* kUa = "com.phoenix.read/73532 (Linux; U; Android 16; zh_CN)";

quint32 rd32(const QByteArray& b, quint64 o) {
    return (quint32(static_cast<unsigned char>(b[int(o)])) << 24)
         | (quint32(static_cast<unsigned char>(b[int(o) + 1])) << 16)
         | (quint32(static_cast<unsigned char>(b[int(o) + 2])) << 8)
         |  quint32(static_cast<unsigned char>(b[int(o) + 3]));
}

quint64 rd64(const QByteArray& b, quint64 o) {
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | quint32(static_cast<unsigned char>(b[int(o) + i]));
    return v;
}

struct Fetched {
    bool ok = false;
    int status = 0;
    QByteArray data;
    quint64 total = 0;
    QString err;
};

// 远端 Range GET(带 UA/跟随跳转);红果 CDN 直连,代理仅在显式设置时走
Fetched fetchRange(const QString& url, quint64 off, quint64 len,
                   const QString& proxy) {
    Fetched f;
    const int sp = url.indexOf(qFromUtf8("://"));
    if (sp < 0) { f.err = qFromUtf8("bad url"); return f; }
    const QString scheme = url.left(sp);
    const int slash = url.indexOf('/', sp + 3);
    const QString hostPort = slash < 0 ? url.mid(sp + 3)
                                       : url.mid(sp + 3, slash - sp - 3);
    const QString path = slash < 0 ? QString() : url.mid(slash);
    const int colon = hostPort.lastIndexOf(':');
    const QString host = colon < 0 ? hostPort : hostPort.left(colon);
    const int port = colon < 0 ? (scheme == qFromUtf8("https") ? 443 : 80)
                              : hostPort.mid(colon + 1).toInt();
    try {
        httplib::Client cli(qFromUtf8("%1://%2:%3").arg(scheme, host).arg(port)
                                .toStdString());
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(30, 0);
        cli.set_write_timeout(10, 0);
        if (!proxy.isEmpty()) {
            const int ps = proxy.indexOf(qFromUtf8("://"));
            const QString hp = ps >= 0 ? proxy.mid(ps + 3) : proxy;
            const int c = hp.lastIndexOf(':');
            cli.set_proxy(hp.left(c).toStdString(), hp.mid(c + 1).toInt());
        }
        httplib::Headers h = {
            { "Range", qFromUtf8("bytes=%1-%2").arg(off).arg(off + len - 1)
                          .toUtf8().constData() },
            { "User-Agent", kUa },
        };
        auto r = cli.Get(path.toStdString(), h);
        if (!r) { f.err = qFromUtf8("connect failed"); return f; }
        f.status = r->status;
        f.data = QByteArray(r->body.data(), int(r->body.size()));
        if (r->status == 206) {
            const QString cr = QString::fromUtf8(
                r->get_header_value("Content-Range").c_str());
            const int sl = cr.lastIndexOf('/');
            if (sl >= 0) f.total = cr.mid(sl + 1).toULongLong();
            f.ok = true;
        } else if (r->status == 200) {
            f.ok = off == 0;
            f.total = quint64(f.data.size());
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
    const QByteArray pat(from, 4);
    int hits = 0;
    int i = int(moov.indexOf(pat, 0));
    while (i >= 8) {
        const quint32 sz = rd32(moov, quint64(i) - 4);
        if (sz >= 8 && quint64(sz) <= quint64(moov.size())) {
            moov.replace(i, 4, QByteArray(to, 4));
            ++hits;
            i = int(moov.indexOf(pat, i + 4));
        } else {
            i = int(moov.indexOf(pat, i + 1));
        }
    }
    return hits > 0;
}

void patchMoov(QByteArray& moov) {
    const bool hevc = moov.contains(QByteArray("hvcC", 4));
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
    quint64 total_ = 0;
    quint64 moovOff = 0;
    QByteArray moovPatched;
    QList<hgcenc::TrackInfo> tracks;

    quint64 total() const override { return total_; }

    bool read(quint64 off, quint64 len, QByteArray* out) override {
        Fetched f = fetchRange(remoteUrl, off, len, proxy);
        if (!f.ok) {
            DBG_P(kMod, Warn, "read failed off=%llu len=%llu: %s",
                  (unsigned long long)off, (unsigned long long)len,
                  qPrintable(f.err));
            return false;
        }
        *out = f.data;
        const quint64 outEnd = off + quint64(out->size());

        // moov 区:改写后的字节直接覆盖(尺寸相同)
        const quint64 moovEnd = moovOff + quint64(moovPatched.size());
        if (moovOff < outEnd && moovEnd > off) {
            const quint64 a = qMax(off, moovOff);
            const quint64 b = qMin(outEnd, moovEnd);
            memcpy(out->data() + int(a - off),
                   moovPatched.constData() + int(a - moovOff), size_t(b - a));
        }

        // 样本区:窗口内就地解;跨窗口样本拉整样本解密后切片
        for (int ti = 0; ti < tracks.size(); ++ti) {
            const hgcenc::TrackInfo& t = tracks[ti];
            for (int i = 0; i < t.sampleOffsets.size(); ++i) {
                const quint64 so = t.sampleOffsets[i];
                const int sz = int(t.sampleSizes[i]);
                if (so >= outEnd) break;
                if (so + quint64(sz) <= off) continue;
                if (so >= off && so + quint64(sz) <= outEnd) {
                    hgcenc::TrackInfo one = t;
                    one.sampleOffsets = QList<quint64>() << (so - off);
                    one.sampleSizes = QList<quint32>() << quint32(sz);
                    one.baseIv = t.baseIv + quint64(i);
                    if (hgcenc::Cenc::decryptTrack(*out, one, key) != 1)
                        return false;
                } else {
                    Fetched sf = fetchRange(remoteUrl, so, quint64(sz), proxy);
                    if (!sf.ok || sf.data.size() != sz) return false;
                    QByteArray one = sf.data;
                    hgcenc::TrackInfo tt = t;
                    tt.sampleOffsets = QList<quint64>() << 0;
                    tt.sampleSizes = QList<quint32>() << quint32(sz);
                    tt.baseIv = t.baseIv + quint64(i);
                    if (hgcenc::Cenc::decryptTrack(one, tt, key) != 1)
                        return false;
                    const quint64 a = qMax(off, so);
                    const quint64 b = qMin(outEnd, so + quint64(sz));
                    memcpy(out->data() + int(a - off),
                           one.constData() + int(a - so), size_t(b - a));
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
    const QByteArray key = QByteArray::fromHex(
        hgcenc::Spade::unwrapV1(QByteArray::fromBase64(spadeA.toUtf8()), 0)
            .toUtf8());
    if (key.size() != 16)
        return Result::fail(Err::Parse, qFromUtf8("hgstream: spade unwrap 失败"));

    // 头部:定位 moov(不足则按精确 Range 补拉)
    Fetched head = fetchRange(remoteUrl, 0, 2u << 20, d->proxy);
    if (!head.ok)
        return Result::fail(Err::Network,
                            qFromUtf8("hgstream: 头部拉取失败 %1").arg(head.err));
    quint64 total = head.total ? head.total : quint64(head.data.size());

    quint64 moovOff = 0, moovSize = 0;
    {
        quint64 o = 0;
        while (o + 8 <= quint64(head.data.size())) {
            const quint64 sz = rd32(head.data, o);
            const quint32 ty = rd32(head.data, o + 4);
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
    if (moovOff + moovSize <= quint64(head.data.size())) {
        moov = head.data.mid(int(moovOff), int(moovSize));
    } else {
        Fetched mf = fetchRange(remoteUrl, moovOff, moovSize, d->proxy);
        if (!mf.ok || quint64(mf.data.size()) < moovSize)
            return Result::fail(Err::Network, qFromUtf8("hgstream: moov 拉取失败"));
        moov = mf.data.left(int(moovSize));
    }

    QList<hgcenc::TrackInfo> tracks;
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

QList<SampleRef> Server::samplesOf(const QString& token, int n) const {
    QList<SampleRef> out;
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
