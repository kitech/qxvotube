// H3a E2E 自检:device_register → multi_video_model → 1080p 密文头 1MB
//                   → spade unwrap → cenc 解密 → NAL 自证
// 口径:docs/protocols/bytedance.md §6.1(均零签名/零真机)。
// 用法:./hgapp [vid]   (缺省用 2026-11-15 实测样本集)
// env:QXVOTUBE_HG_DEVICE_ID / QXVOTUBE_HG_INSTALL_ID 可覆盖设备。
#include "common/hgapp/hgapp.h"
#include "common/hgcenc/spade.h"
#include "common/hgcenc/mp4box.h"
#include "common/hgcenc/cenc.h"
#include "common/netclient.h"
#include "common/debuglog.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

using namespace qxv;
using namespace qxv::hgapp;

namespace {

int g_fail = 0;
QByteArray g_key;

void fail(const char* what, const QString& why) {
    ++g_fail;
    std::printf("[FAIL] %s: %s\n", what, qPrintable(why));
}

// 只解头部 1MB 内完整样本(video/audio 各);视频样本必须全部 NAL 自证通过
void decryptHead(const QByteArray& head) {
    QList<hgcenc::TrackInfo> tracks;
    if (hgcenc::findTracks(head, tracks) != 0) {
        fail("findTracks", qFromUtf8("no encrypted track"));
        return;
    }
    QByteArray buf = head;   // 就地解密副本
    const int n = buf.size();
    for (int ti = 0; ti < tracks.size(); ++ti) {
        const hgcenc::TrackInfo& t = tracks[ti];
        int done = 0, ok = 0;
        for (int i = 0; i < t.sampleOffsets.size(); ++i) {
            const quint64 off = t.sampleOffsets[i];
            const int sz = int(t.sampleSizes[i]);
            if (off + quint64(sz) > quint64(n)) break;   // 头外样本跳过
            hgcenc::TrackInfo one = t;
            one.sampleOffsets = QList<quint64>() << off;
            one.sampleSizes = QList<quint32>() << quint32(sz);
            one.baseIv = t.baseIv + quint64(i);          // 单样本:索引基准平移
            if (hgcenc::Cenc::decryptTrack(buf, one, g_key) != 1) {
                fail("decryptTrack", qFromUtf8("sample %1").arg(i));
                return;
            }
            ++done;
            if (t.isVideo
                && hgcenc::Cenc::nalOk(buf.constData() + int(off), sz)) ++ok;
        }
        if (t.isVideo) {
            if (done == 0 || ok != done) {
                fail("nalOk", qFromUtf8("video %1/%2").arg(ok).arg(done));
                return;
            }
            std::printf("[ ok ] e2e video: %d samples, nal_ok %d/%d\n",
                        done, ok, done);
        } else {
            std::printf("[ ok ] e2e audio: %d samples\n", done);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString vid = argc > 1 ? QString::fromUtf8(argv[1])
                                 : qFromUtf8("7671977925326949400");
    static NetClient fetch;

    ensureDevice([vid](const Result& r, const Device& d) {
        if (r.code != Err::Ok) {
            fail("device_register", r.message);
            QCoreApplication::exit(1);
            return;
        }
        std::printf("[ ok ] device: did=%s iid=%s\n", qPrintable(d.deviceId),
                    qPrintable(d.installId));

        videoModel(vid, [vid](const Result& r2, const QList<Track>& tracks) {
            if (r2.code != Err::Ok) {
                fail("videoModel", r2.message);
                QCoreApplication::exit(1);
                return;
            }
            QStringList defs;
            for (int i = 0; i < tracks.size(); ++i)
                defs << tracks[i].definition + "/" + tracks[i].codecType;
            std::printf("[ ok ] tracks: %d [%s]\n", tracks.size(),
                        qPrintable(defs.join(qFromUtf8(","))));

            const Track* best = pickBest(tracks);
            if (!best) { fail("pickBest", qFromUtf8("none")); QCoreApplication::exit(1); return; }
            std::printf("[ ok ] pick: %s/%s\n", qPrintable(best->definition),
                        qPrintable(best->codecType));

            // spade → content key
            const QByteArray spade = QByteArray::fromBase64(
                best->spadeA.toUtf8());
            const QString keyHex = hgcenc::Spade::unwrapV1(spade, 0);
            if (keyHex.size() != 32) {
                fail("spade unwrap", qFromUtf8("key len %1").arg(keyHex.size()));
                QCoreApplication::exit(1);
                return;
            }
            g_key = QByteArray::fromHex(keyHex.toUtf8());
            std::printf("[ ok ] content key: %s\n", qPrintable(keyHex));

            // 头部 1MB 密文(验证口径:只要开头;完整下载不在自检范围)
            NetRequest fr;
            fr.url = best->url;
            fr.headers = {
                { "Range", "bytes=0-1048575" },
                { "User-Agent",
                  "com.phoenix.read/73532 (Linux; U; Android 16; zh_CN)" },
            };
            fr.timeoutSec = 30;
            fetch.send(fr, [](const NetResponse& nr) {
                if (!nr.ok() || nr.body.isEmpty()) {
                    fail("streamhead", nr.error.isEmpty()
                             ? qFromUtf8("status %1").arg(nr.status) : nr.error);
                    QCoreApplication::exit(1);
                    return;
                }
                std::printf("[ ok ] streamhead: %d bytes\n", nr.body.size());
                decryptHead(nr.body);
                std::printf("hgapp: %s\n", g_fail ? "FAILED" : "all green");
                QCoreApplication::exit(g_fail ? 1 : 0);
            });
        });
    });

    QTimer::singleShot(120000, &app, []() {
        std::printf("[TIMEOUT] 120s\n");
        QCoreApplication::exit(2);
    });
    return app.exec();
}
