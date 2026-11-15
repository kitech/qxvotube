// H2b 自检:本地流式解密代理(Range/seek/会话回收/空闲关/真实解码)
// 需外网:先走 hgapp 取 1080p 轨道,再对本机代理做 Range 校验。
// 手动运行:./hgstream            (缺省样本集)
//           ./hgstream <vid>
// env:QXVOTUBE_HG_IDLE_SEC / _SESS_TTL_SEC 由测试内置覆盖(短超时便于验证)。
#include "common/hgapp/hgapp.h"
#include "common/hgstream/hgstream.h"
#include "common/hgcenc/cenc.h"
#include "common/debuglog.h"

#include "httplib.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <string>

using namespace qxv;

namespace {

int g_fail = 0;

void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) {
        std::printf("[ ok ] %s\n", what);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s %s\n", what, qPrintable(got));
    }
}

struct HttpResult {
    int status = 0;
    QByteArray body;
    quint64 total = 0;
};

HttpResult getRange(int port, const QString& path, quint64 off, quint64 len) {
    HttpResult r;
    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(30, 0);
    httplib::Headers h = { { "Range", qFromUtf8("bytes=%1-%2")
                                        .arg(off).arg(off + len - 1)
                                        .toUtf8().constData() } };
    auto res = cli.Get(path.toStdString(), h);
    if (!res) return r;
    r.status = res->status;
    r.body = QByteArray(res->body.data(), int(res->body.size()));
    const QString cr = QString::fromUtf8(
        res->get_header_value("Content-Range").c_str());
    const int sl = cr.lastIndexOf('/');
    if (sl >= 0) r.total = cr.mid(sl + 1).toULongLong();
    return r;
}

// 窗口内 NAL 自证:用代理会话的样本索引,取完整落在窗口内的视频样本
int nalOkInWindow(hgstream::Server& srv, const QString& token,
                  const QByteArray& body, quint64 winOff, int want) {
    const QList<hgstream::Server::SampleRef> ss = srv.samplesOf(token, 4096);
    int ok = 0, done = 0;
    for (int i = 0; i < ss.size() && done < want; ++i) {
        if (!ss[i].video) continue;
        const quint64 so = ss[i].offset;
        if (so < winOff || so + quint64(ss[i].size) > winOff + quint64(body.size()))
            continue;
        ++done;
        if (hgcenc::Cenc::nalOk(body.constData() + int(so - winOff),
                                ss[i].size))
            ++ok;
    }
    std::printf("       %s: nal_ok %d/%d\n", qPrintable(token.left(8)), ok, done);
    return done > 0 && ok == done ? ok : -1;
}

void runChecks(hgapp::Track track) {
    hgstream::Server& srv = hgstream::Server::instance();
    hgstream::Options o;
    o.idleSec = 8;             // 空闲关:8s
    o.sessionIdleSec = 2;      // 会话回收:2s
    o.maxSessions = 2;
    srv.configure(o);

    QString url;
    Result r = srv.open(QString::fromUtf8("probe"), track.definition,
                        track.url, track.spadeA, &url);
    check(r.code == Err::Ok, "open session", r.message);
    if (r.code != Err::Ok) { QCoreApplication::exit(1); return; }
    std::printf("[info] local url: %s\n", qPrintable(url));

    const int port = srv.port();
    const QString path = url.mid(url.indexOf('/', 7));   // /hg/<token>.mp4
    const QString token = srv.tokenOf(QString::fromUtf8("probe"),
                                      track.definition);

    // 1) 头部 1MB
    HttpResult a = getRange(port, path, 0, 1048576);
    check(a.status == 206, "range head 206",
          qFromUtf8("status=%1").arg(a.status));
    check(a.total > 0, "content-range total", QString::number(a.total));
    check(a.body.size() == 1048576, "range head size",
          QString::number(a.body.size()));
    // moov 同长度改写断言(密文容器变明文容器)
    check(!a.body.contains(QByteArray("encv", 4)), "moov: encv removed");
    check(!a.body.contains(QByteArray("sinf", 4)), "moov: sinf removed");
    check(a.body.contains(QByteArray("hvc1", 4))
              || a.body.contains(QByteArray("avc1", 4)),
          "moov: codec tag rewritten");
    check(nalOkInWindow(srv, token, a.body, 0, 40) > 0,
          "decrypted video NAL #1");

    // 2) 中段(seek 场景)
    const quint64 mid = a.total / 2;
    const quint64 midLen = 262144;
    HttpResult b = getRange(port, path, mid, midLen);
    check(b.status == 206, "range mid 206");
    check(quint64(b.body.size()) == midLen, "range mid size",
          QString::number(b.body.size()));
    check(nalOkInWindow(srv, token, b.body, mid, 20) > 0,
          "decrypted video NAL mid");

    // 3) 尾部
    const quint64 tailLen = 4096;
    HttpResult c = getRange(port, path, a.total - tailLen, tailLen);
    check(c.status == 206 && quint64(c.body.size()) == tailLen, "range tail 206");
    check(c.total == a.total, "range tail total");

    // 4) 越界 → 416
    HttpResult d = getRange(port, path, a.total + 10, 100);
    check(d.status == 416, "range unsatisfiable 416",
          qFromUtf8("status=%1").arg(d.status));

    // 5) 真实解码(ffprobe;不存在则跳过)
    {
        const std::string cmd = "ffprobe -v error -show_entries "
                                "stream=codec_name -of csv=p=0 '" + url.toStdString()
                                + "' 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        QString out;
        if (p) {
            char buf[256];
            while (fgets(buf, sizeof(buf), p)) out += QString::fromUtf8(buf);
            pclose(p);
        }
        check(out.contains(qFromUtf8("hevc")) || out.contains(qFromUtf8("h264")),
              "ffprobe decodes codec", out.trimmed());
        // 真解码 30 帧(ffprobe 只读头;这里让解码器真吃数据)
        const std::string dcmd = "ffmpeg -v error -i '" + url.toStdString()
                               + "' -frames:v 30 -f null - 2>&1";
        FILE* dp = popen(dcmd.c_str(), "r");
        QString derr;
        int drc = -1;
        if (dp) {
            char buf[512];
            while (fgets(buf, sizeof(buf), dp)) derr += QString::fromUtf8(buf);
            drc = pclose(dp);
        }
        check(drc == 0 && derr.trimmed().isEmpty(),
              "ffmpeg decodes 30 frames", derr.trimmed().left(300));
    }

    // 6) 会话回收(释放引用 + 2s 静默)
    srv.release(QString::fromUtf8("probe"), track.definition);
    QThread::sleep(4);
    check(srv.stats().sessions == 0, "session evicted after release+idle",
          QString::number(srv.stats().sessions));

    // 7) 空闲自动关(8s 无请求) + 重启
    QThread::sleep(7);
    check(!srv.stats().running, "server idle-stopped",
          qFromUtf8("running=%1").arg(srv.stats().running));
    check(srv.stats().lastStopReason == qFromUtf8("idle"),
          "idle stop reason", srv.stats().lastStopReason);

    QString url2;
    Result r2 = srv.open(QString::fromUtf8("probe"), track.definition,
                         track.url, track.spadeA, &url2);
    check(r2.code == Err::Ok && srv.stats().running, "server restarted",
          r2.message);
    if (r2.code == Err::Ok) {
        const QString p2 = url2.mid(url2.indexOf('/', 7));
        HttpResult e = getRange(srv.port(), p2, 0, 65536);
        check(e.status == 206 && !e.body.isEmpty(), "restarted stream serves");
        srv.release(QString::fromUtf8("probe"), track.definition);
    }
    srv.stop(qFromUtf8("test-end"));
    check(!srv.stats().running, "explicit stop");

    std::printf("hgstream: %s\n", g_fail ? "FAILED" : "all green");
    QCoreApplication::exit(g_fail ? 1 : 0);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString vid = argc > 1 ? QString::fromUtf8(argv[1])
                                 : qFromUtf8("7671977925326949400");

    hgapp::videoModel(vid, [](const Result& r, const QList<hgapp::Track>& tracks) {
        if (r.code != Err::Ok) {
            std::printf("[FAIL] videoModel: %s\n", qPrintable(r.message));
            QCoreApplication::exit(1);
            return;
        }
        const hgapp::Track* best = hgapp::pickBest(tracks);
        if (!best) {
            std::printf("[FAIL] no track\n");
            QCoreApplication::exit(1);
            return;
        }
        std::printf("[ ok ] track: %s/%s\n", qPrintable(best->definition),
                    qPrintable(best->codecType));
        runChecks(*best);
    });

    QTimer::singleShot(180000, &app, []() {
        std::printf("[TIMEOUT] 180s\n");
        QCoreApplication::exit(2);
    });
    return app.exec();
}
