// providers 自检:逐平台最小请求(search→resolve),打印可达性表
// 用法:./test_smoke [bilibili|youtube|all]  (缺省 all)
#include "compat.h"
#include "common/provider.h"
#include "common/debuglog.h"
#include "common/netclient.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

using namespace qxv;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();
    dbg::setLevel(QString(), dbg::Debug);

    const QString only = argc > 1 ? QString::fromUtf8(argv[1]) : QStringLiteral("all");
    const QList<VideoProvider*> provs = createProviders();
    int pending = 0;
    int failures = 0;
    auto done = [&pending, &failures]() {
        if (--pending == 0)
            QCoreApplication::exit(failures == 0 ? 0 : 1);
    };

    for (VideoProvider* p : provs) {
        if (only != QStringLiteral("all") && p->id() != only) continue;
        ++pending;
        const QString pid = p->id();
        p->search(QXV_STR("travel"), 1,
            [p, &pending, &failures, &done, pid](Result res,
                                          const QList<VideoItem>& items, bool) {
            if (res.code != Err::Ok || items.isEmpty()) {
                std::printf("[FAIL] %-9s search: code=%d msg=%s\n",
                            qPrintable(pid), int(res.code),
                            qPrintable(res.message));
                ++failures;
                done(); return;
            } else {
                std::printf("[ ok ] %-9s search: %d items, first=%s\n",
                            qPrintable(pid), items.size(),
                            qPrintable(items.first().title));
                // 再走 resolve 链
                const QString vid = items.first().id;
                p->detail(vid, [p, vid, pid, &pending, &failures, &done](
                                   Result res2, const VideoItem&,
                                   const QList<VideoItem>& parts) {
                    if (res2.code != Err::Ok || parts.isEmpty()) {
                        std::printf("[FAIL] %-9s detail %s: %s\n",
                                    qPrintable(pid), qPrintable(vid),
                                    qPrintable(res2.message));
                        ++failures;
                        done(); return;
                    } else {
                        p->resolve(vid,      // resolve 入参=视频 id(非分集 cid)
                            [pid, &pending, &failures, &done](Result res3,
                                                       const PlayResult& pr) {
                            if (res3.code != Err::Ok || pr.streams.isEmpty()) {
                                std::printf("[FAIL] %-9s resolve: %s\n",
                                            qPrintable(pid),
                                            qPrintable(res3.message));
                                ++failures;
                            } else {
                                std::printf("[ ok ] %-9s resolve: %d streams, "
                                            "url=%s\n", qPrintable(pid),
                                            pr.streams.size(),
                                            qPrintable(pr.streams.first()
                                                           .url.left(60)));
                                // 流头部拉取(≤1MB)验证可播:
                                // youtube(googlevideo 被墙)走代理,口径
                                // curl -r 0-1048575 -x http://127.0.0.1:10809
                                // fetch 必须 static:send 异步,栈上对象会先析构
                                static NetClient fetch;
                                NetRequest fr;
                                fr.url = pr.streams.first().url;
                                fr.headers = {
                                    { "Range", "bytes=0-1048575" },
                                    { "User-Agent",
                                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                      " AppleWebKit/537.36" },
                                };
                                if (pid == QXV_STR("youtube"))
                                    fr.proxy = QXV_ENV("QXVOTUBE_PROXY");
                                fetch.send(fr, [pid, &done, &failures](
                                                   const NetResponse& nr) {
                                    const int n = nr.body.size();
                                    if (nr.ok() && n > 0) {
                                        std::printf("[ ok ] %-9s streamhead: "
                                                    "%d bytes, head=%.8s\n",
                                                    qPrintable(pid), n,
                                                    nr.body.constData());
                                    } else {
                                        std::printf("[FAIL] %-9s streamhead: "
                                                    "status=%d %s\n",
                                                    qPrintable(pid), nr.status,
                                                    qPrintable(nr.error));
                                        ++failures;
                                    }
                                    done();
                                });
                                return;   // done() 由 fetch 回调统一调
                            }
                            done();
                        });
                    }
                    Q_UNUSED(p);
                });
                return;   // pending 在 resolve 完成时才减
            }
            done();
        });
    }
    QTimer::singleShot(90000, &app, []() {
        std::printf("[TIMEOUT] 90s\n");
        QCoreApplication::exit(2);
    });
    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    return rc;
}
