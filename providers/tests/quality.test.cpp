// 清晰度选择验收(resolveQuality):
//   bilibili 1080p → 服务端实际 720P,标 degraded
//   bilibili 720p  → 720P,不标 degraded
//   hongguo  1080p → 1080P/bytevc1(本地解密代理)
//   hongguo  720p  → Err::Unsupported(bytevc2 私有编码)
//   youtube  1080p → 360p + degraded(仅 itag18;DASH 合流是 Wave D)
// 用法:./quality [bilibili|hongguo|youtube|all]   (需外网;youtube 需 QXVOTUBE_PROXY)
#include "common/provider.h"
#include "common/debuglog.h"
#include "common/ytdlp.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <memory>

using namespace qxv;

namespace {

int g_fail = 0;

struct Case {
    QString prov;
    QString quality;
    bool expectOk;
    bool expectDegraded;
    QString expectQualityPrefix;   // 空=不校验
    Err expectErr = Err::Unsupported;   // expectOk=false 时的期望错误码
};

void expect(bool ok, const QString& what) {
    if (ok) std::printf("[ ok ] %s\n", qPrintable(what));
    else { ++g_fail; std::printf("[FAIL] %s\n", qPrintable(what)); }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString only = argc > 1 ? QString::fromUtf8(argv[1]) : qFromUtf8("all");
    const QList<VideoProvider*> provs = createProviders();

    const QList<Case> cases = {
        // bilibili html5 只有 360P/720P:160/240/480/1080 均就近降级
        { qFromUtf8("bilibili"), qFromUtf8("160p"),  true, true,  qFromUtf8("360P") },
        { qFromUtf8("bilibili"), qFromUtf8("240p"),  true, true,  qFromUtf8("360P") },
        { qFromUtf8("bilibili"), qFromUtf8("480p"),  true, true,  qFromUtf8("360P") },
        { qFromUtf8("bilibili"), qFromUtf8("720p"),  true, false, qFromUtf8("720P") },
        { qFromUtf8("bilibili"), qFromUtf8("1080p"), true, true,  qFromUtf8("720P") },
        // 红果:1080p 走 App(bytevc1 本地解密);≤720p 走官网明文 360P(bytevc2 不可播)
        { qFromUtf8("hongguo"),  qFromUtf8("1080p"), true, false, qFromUtf8("1080P") },
        { qFromUtf8("hongguo"),  qFromUtf8("360p"),  true, false, qFromUtf8("360P") },
        { qFromUtf8("hongguo"),  qFromUtf8("160p"),  true, true,  qFromUtf8("360P") },
        // youtube:≤360p 走 itag18(标降级);>360p 走 yt-dlp HLS(不降级)
        { qFromUtf8("youtube"),  qFromUtf8("160p"),  true, true,  QString() },
        { qFromUtf8("youtube"),  qFromUtf8("240p"),  true, true,  QString() },
        { qFromUtf8("youtube"),  qFromUtf8("1080p"), true,
          !ytdlp::enabled(), QString() },
    };

    // 先按平台取一个可播 id
    auto ids = std::make_shared<QMap<QString, QString>>();
    auto remaining = std::make_shared<int>(0);
    for (VideoProvider* p : provs) {
        if (only != qFromUtf8("all") && p->id() != only) continue;
        ++*remaining;
        const QString pid = p->id();
        p->search(qFromUtf8("travel"), 1,
            [ids, remaining, pid](Result r, const QList<VideoItem>& items, bool) {
            if (r.code == Err::Ok && !items.isEmpty())
                ids->insert(pid, items.first().id);
            --*remaining;
        });
    }

    auto idx = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();
    *next = [&provs, cases, idx, next, ids, only]() {
        while (*idx < cases.size()) {
            const Case c = cases[(*idx)++];
            if (only != qFromUtf8("all") && c.prov != only) continue;
            VideoProvider* p = nullptr;
            for (VideoProvider* q : provs) if (q->id() == c.prov) p = q;
            if (!p || !ids->contains(c.prov)) {
                expect(false, qFromUtf8("%1: 无 id(搜索失败)").arg(c.prov));
                continue;
            }
            const QString id = ids->value(c.prov);
            p->resolveQuality(id, c.quality,
                [c, next](Result r, const PlayResult& pr) {
                const QString tag = qFromUtf8("%1 %2").arg(c.prov, c.quality);
                if (!c.expectOk) {
                    expect(r.code == c.expectErr,
                           tag + qFromUtf8(" → %1 期望, got %2")
                                     .arg(int(c.expectErr)).arg(int(r.code)));
                    (*next)();
                    return;
                }
                if (r.code != Err::Ok || pr.streams.isEmpty()) {
                    expect(false, tag + qFromUtf8(" → ok 期望, got %1 %2")
                                             .arg(QString::number(int(r.code)),
                                                  r.message));
                    (*next)();
                    return;
                }
                const Stream& s = pr.streams.first();
                const bool degraded = s.extra.value(qFromUtf8("degraded")).toBool();
                QString detail = qFromUtf8("%1 → quality=%2 degraded=%3")
                                     .arg(tag, s.quality, degraded ? "1" : "0");
                bool ok = (degraded == c.expectDegraded);
                if (!c.expectQualityPrefix.isEmpty())
                    ok = ok && s.quality.startsWith(c.expectQualityPrefix);
                expect(ok, detail);
                (*next)();
            });
            return;
        }
        QCoreApplication::exit(g_fail ? 1 : 0);
    };

    // 等搜索回调全部回来后再跑用例
    QTimer* poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, &app, [remaining, next]() {
        static bool started = false;
        if (!started && *remaining == 0) { started = true; (*next)(); }
    });
    poll->start(200);
    QTimer::singleShot(240000, &app, []() {
        std::printf("[TIMEOUT] 240s\n");
        QCoreApplication::exit(2);
    });

    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    std::printf("quality: %s\n", g_fail ? "FAILED" : "all green");
    return rc;
}
