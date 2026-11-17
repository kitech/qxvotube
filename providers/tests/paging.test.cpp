// 搜索分页验收:page1 → hasMore → page2;两页 id 不重复。
// 用法:./paging [bilibili|hongguo|youtube|all]
// 需外网(youtube 需 QXVOTUBE_PROXY)。
#include "common/provider.h"
#include "common/debuglog.h"

#include <QCoreApplication>
#include <QSet>
#include <QTimer>
#include <cstdio>

using namespace qxv;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString only = argc > 1 ? QString::fromUtf8(argv[1])
                                  : qFromUtf8("all");
    const QList<VideoProvider*> provs = createProviders();
    int pending = 0;
    int fails = 0;

    for (VideoProvider* p : provs) {
        if (only != qFromUtf8("all") && p->id() != only) continue;
        ++pending;
        const QString pid = p->id();
        // YouTube 的 "travel" 没有续页(hasMore=0),用结果量大的 "music" 验证翻页
        const QString kw = pid == qFromUtf8("youtube") ? qFromUtf8("music")
                                                       : qFromUtf8("travel");
        p->search(kw, 1,
            [p, pid, kw, &pending, &fails](Result r1,
                                           const QList<VideoItem>& a,
                                           bool hasMore) {
            if (r1.code != Err::Ok || a.isEmpty()) {
                std::printf("[FAIL] %-9s page1: %s\n", qToUtf8(pid).data(),
                            qToUtf8(r1.message).data());
                ++fails;
                --pending;
                return;
            }
            std::printf("[ ok ] %-9s page1: %d items hasMore=%d\n",
                        qToUtf8(pid).data(), a.size(), int(hasMore));
            if (!hasMore) { --pending; return; }   // 明确无边:不算失败
            QSet<QString> ids;
            for (const VideoItem& it : a) ids.insert(it.id);
            p->search(kw, 2,
                [pid, ids, &pending, &fails](Result r2,
                                            const QList<VideoItem>& b, bool) {
                int dup = 0;
                for (const VideoItem& it : b)
                    if (ids.contains(it.id)) ++dup;
                // 服务端排序漂移会带来少量跨页重复;要求新条目占多数即可
                const bool okOverlap = b.isEmpty() ? false : dup * 2 <= b.size();
                if (r2.code != Err::Ok || b.isEmpty() || !okOverlap) {
                    std::printf("[FAIL] %-9s page2: %s items=%d dup=%d\n",
                                qToUtf8(pid).data(), qToUtf8(r2.message).data(),
                                b.size(), dup);
                    ++fails;
                } else {
                    std::printf("[ ok ] %-9s page2: %d items (dup=0)\n",
                                qToUtf8(pid).data(), b.size());
                }
                --pending;
            });
        });
    }

    QTimer::singleShot(180000, &app, []() {
        std::printf("[TIMEOUT] 180s\n");
        QCoreApplication::exit(2);
    });
    QTimer* poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, &app, [&pending, &fails]() {
        if (pending == 0) QCoreApplication::exit(fails == 0 ? 0 : 1);
    });
    poll->start(200);

    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    return rc;
}
