// Qt3 兼容验收:hgcenc 的 spade KAT + mp4box 空输入 + localstream/ytdlp 编译链接
// (字节级移植后必须真实可跑;非 Qt6 ctest 的替代)
#include "common/hgcenc/spade.h"
#include "common/hgcenc/mp4box.h"
#include "common/localstream/localstream.h"
#include "common/ytdlp.h"

#include <qapplication.h>
#include <qtimer.h>
#include <qstring.h>
#include <qstringlist.h>

#include "httplib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using qxv::hgcenc::Spade;

namespace {

int g_fail = 0;

void expect(bool ok, const char* what, const QString& got = QString(),
            const QString& exp = QString()) {
    if (ok) {
        std::printf("[ ok ] %s\n", what);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s got=%s exp=%s\n", what,
                    qToUtf8(got).data(), qToUtf8(exp).data());
    }
}

QByteArray fromHex(const char* hex) {
    std::string out;
    const int n = (int)strlen(hex);
    for (int i = 0; i + 1 < n; i += 2) {
        int hi = hex[i] >= 'a' ? (hex[i] - 'a' + 10) : (hex[i] - '0');
        int lo = hex[i + 1] >= 'a' ? (hex[i + 1] - 'a' + 10) : (hex[i + 1] - '0');
        out.push_back((char)((hi << 4) | lo));
    }
    return qbaFromData(out.data(), (int)out.size());
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Q_UNUSED(app);

    struct V { const char* spadeHex; const char* key; };
    static const V kTruth[] = {
        { "93bc1df253ba1bf7618b19c448b806f64d810afc45b715fe5eb925f26cbf12f541ba098282",
          "287216bfa89e662a0f748120c305e199" },
        { "a1bc2ff164b91df04dbf00f17dba00f47cbb35c27b922aec67a628eb52972fdc7ea036a7a7",
          "d742b28967e4e6c92b699f4375add27f" },
        { "a3bc2df760ba2bc27b8b34c04a8907f557be19f249be35c748ba03f64fbc1ff161ba29a2a2",
          "b5674a8384f25d757585dadf343274d5" },
    };
    for (int i = 0; i < (int)(sizeof(kTruth) / sizeof(kTruth[0])); ++i) {
        const QString got = Spade::unwrapV1(fromHex(kTruth[i].spadeHex), 0);
        expect(got == qFromUtf8(kTruth[i].key), "spade truth", got,
               qFromUtf8(kTruth[i].key));
    }
    expect(Spade::unwrapV1(qbaFromCharptr("ab"), 0).isEmpty(),
           "spade short input rejected");

    // mp4box:空/垃圾输入必须显式失败,不越界
    QList36<qxv::hgcenc::TrackInfo> tracks;
    expect(qxv::hgcenc::findTracks(QByteArray(), tracks) < 0,
           "mp4box empty input rejected");

    // localstream:真实起服务 + Range 取两段(证明本地代理链路可用)
    {
        using namespace qxv::localstream;
        std::shared_ptr<Source> src(new BufferSource(
            qbaFromCharptr("0123456789"), qFromUtf8("text/plain")));
        QString url;
        qxv::Result o = Server::instance().open(qFromUtf8("hgcompat"), src, &url);
        expect(o.code == qxv::Err::Ok && !url.isEmpty(),
               "localstream open -> url", url);
        if (!url.isEmpty()) {
            // http://127.0.0.1:PORT/hg/<token>
            const QString hp = url.mid(7);
            const int slash = qIndexOf(hp, qFromUtf8("/"));
            const QString hostPort = hp.left(slash);
            const QString path = hp.mid(slash);
            const int colon = qLastIndexOf(hostPort, qFromUtf8(":"));
            httplib::Client cli(std::string(
                qToUtf8(qFromUtf8("http://") + hostPort).data()));
            httplib::Headers h;
            h.emplace("Range", std::string("bytes=2-5"));
            auto r = cli.Get(std::string(qToUtf8(path).data()), h);
            expect(r && r->status == 206 && r->body == "2345",
                   "localstream Range 206/2345",
                   r ? qFromUtf8(r->body.c_str()) : QString("no-response"));
        }
        Server::instance().release(qFromUtf8("hgcompat"));
    }

    // ytdlp 运行时(opt-in;需网络/代理):QXVOTUBE_TEST_YTDLP=<url>
    if (!qenv("QXVOTUBE_TEST_YTDLP").isEmpty()) {
        bool done = false;
        qxv::ytdlp::fetch(qenv("QXVOTUBE_TEST_YTDLP"),
                          qenv("QXVOTUBE_PROXY"),
                          [&](const qxv::ytdlp::Info& info) {
            expect(info.ok && !info.formats.isEmpty(),
                   "ytdlp fetch formats", QString::number(info.formats.size()),
                   info.err);
            done = true;
            qApp->quit();
        });
        if (!done) {
            QTimer::singleShot(90000, &app, SLOT(quit()));
            app.exec();
        }
    }

    std::printf("hgcompat: %s\n", g_fail ? "FAILED" : "all green");
    return g_fail ? 1 : 0;
}
