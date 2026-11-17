// YouTube 高清晰度(HLS 合流)验收(需外网 + 代理 + yt-dlp + ffmpeg):
//   1) resolveQuality("1080p") → kind=hls + 127.0.0.1 本地 URL + extra.hlsMaster
//   2) ffprobe 断言分辨率/编码
//   3) ffmpeg 解 30 帧(分片走 googlevideo,需代理)
// 用法:QXVOTUBE_PROXY=http://127.0.0.1:10809 ./ytdash [videoId]
#include "common/provider.h"
#include "common/debuglog.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <cstdio>

using namespace qxv;

namespace {
int g_fail = 0;
void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qToUtf8(got).data()); }
}

QString run(const QString& prog, const QStringList& args, int timeoutMs,
            int* code, const QString& proxy) {
    QProcess p;
    if (!proxy.isEmpty()) {
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert(qFromUtf8("http_proxy"), proxy);
        e.insert(qFromUtf8("https_proxy"), proxy);
        p.setProcessEnvironment(e);
    }
    p.start(prog, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        if (code) *code = -1;
        return QString::fromUtf8(p.readAllStandardError());
    }
    if (code) *code = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput())
         + QString::fromUtf8(p.readAllStandardError());
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString id = argc > 1 ? QString::fromUtf8(argv[1])
                                : qFromUtf8("0q7siV1PH2g");
    const QString proxy = qenv("QXVOTUBE_PROXY");

    const QList<VideoProvider*> provs = createProviders();
    VideoProvider* yt = nullptr;
    for (VideoProvider* p : provs)
        if (p->id() == qFromUtf8("youtube")) yt = p;
    if (!yt) { std::printf("[FAIL] no youtube provider\n"); return 1; }

    yt->resolveQuality(id, qFromUtf8("1080p"),
        [proxy](Result r, const PlayResult& pr) {
        check(r.code == Err::Ok && !pr.streams.isEmpty(), "resolveQuality 1080p",
              r.message);
        if (r.code != Err::Ok || pr.streams.isEmpty()) {
            QCoreApplication::exit(1);
            return;
        }
        const Stream& s = pr.streams.first();
        check(s.kind == qFromUtf8("hls"), "kind=hls", s.kind);
        check(s.url.startsWith(qFromUtf8("http://127.0.0.1:")), "本地 URL",
              s.url);
        check(s.extra.value(qFromUtf8("hlsMaster")).toBool(), "extra.hlsMaster");
        std::printf("[info] url=%s\n", qToUtf8(s.url).data());

        int code = -1;
        const QString probe = run(qFromUtf8("ffprobe"),
            { qFromUtf8("-v"), qFromUtf8("error"),
              qFromUtf8("-show_entries"), qFromUtf8("stream=codec_name,width,height"),
              qFromUtf8("-of"), qFromUtf8("csv=p=0"), s.url },
            60000, &code, proxy);
        check(code == 0 && probe.contains(qFromUtf8("1920,1080")),
              "ffprobe 1080p", qTrim(probe).left(120));

        const QString dec = run(qFromUtf8("ffmpeg"),
            { qFromUtf8("-v"), qFromUtf8("error"), qFromUtf8("-i"), s.url,
              qFromUtf8("-frames:v"), qFromUtf8("30"), qFromUtf8("-f"),
              qFromUtf8("null"), qFromUtf8("-") },
            90000, &code, proxy);
        check(code == 0, "ffmpeg 解码 30 帧", qTrim(dec).left(200));

        std::printf("ytdash: %s\n", g_fail ? "FAILED" : "all green");
        QCoreApplication::exit(g_fail ? 1 : 0);
    });

    QTimer::singleShot(180000, &app, []() {
        std::printf("[TIMEOUT]\n");
        QCoreApplication::exit(2);
    });
    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    return rc;
}
