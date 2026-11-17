// detail 补全验收:
//   bilibili: 分P id 为 "bvid.cid" 复合形式,可直接喂 resolveQuality
//   红果:     分集列表 ≥5,每集 extra.seriesId 正确
//   youtube:  单视频 title 非空;播放列表 → 分集 id 列表
// 用法:./detail [bilibili|hongguo|youtube|all]  (需外网;youtube 需 QXVOTUBE_PROXY)
#include "common/provider.h"
#include "common/debuglog.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

using namespace qxv;

namespace {
int g_fail = 0;
void check(bool ok, const QString& what) {
    if (ok) std::printf("[ ok ] %s\n", qToUtf8(what).data());
    else { ++g_fail; std::printf("[FAIL] %s\n", qToUtf8(what).data()); }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString only = argc > 1 ? QString::fromUtf8(argv[1]) : qFromUtf8("all");
    const QList<VideoProvider*> provs = createProviders();
    int pending = 0;

    for (VideoProvider* p : provs) {
        if (only != qFromUtf8("all") && p->id() != only) continue;
        ++pending;
        const QString pid = p->id();

        auto finish = [&pending]() { --pending; };

        if (pid == qFromUtf8("youtube")) {
            // 单视频
            p->detail(qFromUtf8("0q7siV1PH2g"),
                [p, pid, finish](Result r, const VideoItem& vi,
                                 const QList<VideoItem>&) {
                check(r.code == Err::Ok && !vi.title.isEmpty(),
                      qFromUtf8("%1 detail(video): title=%2")
                          .arg(pid, vi.title.left(24)));
                // 播放列表
                p->detail(qFromUtf8("PLBCF2DAC6FFB574DE"),
                    [pid, finish](Result r2, const VideoItem&,
                                  const QList<VideoItem>& parts) {
                    check(r2.code == Err::Ok && parts.size() >= 10,
                          qFromUtf8("%1 detail(playlist): %2 items")
                              .arg(pid).arg(parts.size()));
                    finish();
                });
            });
            continue;
        }

        p->search(qFromUtf8("travel"), 1,
            [p, pid, finish](Result sr, const QList<VideoItem>& items, bool) {
            if (sr.code != Err::Ok || items.isEmpty()) {
                check(false, qFromUtf8("%1 search failed: %2")
                                 .arg(pid, sr.message));
                finish();
                return;
            }
            const QString id = items.first().id;
            p->detail(id, [p, pid, id, finish](Result r, const VideoItem&,
                                               const QList<VideoItem>& parts) {
                if (r.code != Err::Ok || parts.isEmpty()) {
                    check(false, qFromUtf8("%1 detail: %2").arg(pid, r.message));
                    finish();
                    return;
                }
                if (pid == qFromUtf8("bilibili")) {
                    const QString composite = parts.first().id;
                    check(composite.contains(QLatin1Char('.')),
                          qFromUtf8("bilibili part.id=%1 (bvid.cid)")
                              .arg(composite));
                    // 复合 id 直接可播(分P解析)
                    p->resolveQuality(composite, qFromUtf8("360p"),
                        [pid, composite, finish](Result rr,
                                                 const PlayResult& pr) {
                        check(rr.code == Err::Ok && !pr.streams.isEmpty(),
                              qFromUtf8("%1 resolveQuality(%2) ok")
                                  .arg(pid, composite));
                        finish();
                    });
                    return;
                }
                // hongguo
                bool seriesOk = true;
                for (const VideoItem& it : parts)
                    if (it.extra.value(qFromUtf8("seriesId")).toString() != id)
                        seriesOk = false;
                check(parts.size() >= 5 && seriesOk,
                      qFromUtf8("%1 detail: %2 episodes (seriesId 一致=%3)")
                          .arg(pid).arg(parts.size()).arg(int(seriesOk)));
                finish();
            });
        });
    }

    QTimer::singleShot(180000, &app, []() {
        std::printf("[TIMEOUT] 180s\n");
        QCoreApplication::exit(2);
    });
    QTimer* poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, &app, [&pending]() {
        if (pending == 0) QCoreApplication::exit(g_fail ? 1 : 0);
    });
    poll->start(200);

    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    std::printf("detail: %s\n", g_fail ? "FAILED" : "all green");
    return rc;
}
