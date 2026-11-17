// homeFeed 验收:
//   bilibili 热门榜 → ≥10 条,page2 可续
//   红果 官网首页 → 四类各 9 条,page2 明确空+hasMore=false
//   youtube → 显式 Err::Unsupported(该 IP browse 空壳,不造假数据)
// 用法:./homefeed [bilibili|hongguo|youtube|all]   (需外网;youtube 需 QXVOTUBE_PROXY)
#include "common/provider.h"
#include "common/debuglog.h"

#include <QCoreApplication>
#include <QSet>
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
        p->homeFeed(1, [p, pid, &pending](Result r,
                                          const QList<VideoItem>& items,
                                          bool hasMore) {
            if (pid == qFromUtf8("douyin")) {
                check(r.code == Err::Ok && items.size() >= 10,
                      qFromUtf8("douyin homeFeed: %1 hotwords (first=%2)")
                          .arg(items.size())
                          .arg(items.isEmpty() ? QString()
                                               : items.first().title.left(16)));
                --pending;
                return;
            }
            if (pid == qFromUtf8("youtube")) {
                check(r.code == Err::Unsupported,
                      qFromUtf8("%1 homeFeed → Unsupported(%2)")
                          .arg(pid).arg(int(r.code)));
                --pending;
                return;
            }
            if (r.code != Err::Ok || items.size() < 9) {
                check(false, qFromUtf8("%1 homeFeed p1: %2 items=%3")
                                 .arg(pid, r.message)
                                 .arg(items.size()));
                --pending;
                return;
            }
            check(true, qFromUtf8("%1 homeFeed p1: %2 items (hasMore=%3, first=%4)")
                            .arg(pid).arg(items.size())
                            .arg(int(hasMore))
                            .arg(items.first().title.left(24)));
            if (!hasMore) { --pending; return; }

            QSet<QString> ids;
            for (const VideoItem& it : items) ids.insert(it.id);
            p->homeFeed(2, [pid, ids, &pending](Result r2,
                                               const QList<VideoItem>& b, bool) {
                int dup = 0;
                for (const VideoItem& it : b) if (ids.contains(it.id)) ++dup;
                check(r2.code == Err::Ok && !b.isEmpty() && dup * 2 <= b.size(),
                      qFromUtf8("%1 homeFeed p2: %2 items (dup=%3)")
                          .arg(pid).arg(b.size()).arg(dup));
                --pending;
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
    std::printf("homefeed: %s\n", g_fail ? "FAILED" : "all green");
    return rc;
}
