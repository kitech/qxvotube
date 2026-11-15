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

    // 用法:./pvsmoke [平台] [-q 720p]
    const QString only = argc > 1 && argv[1][0] != '-' ? QString::fromUtf8(argv[1])
                                                       : QStringLiteral("all");
    QString wantQuality;
    for (int i = 1; i + 1 < argc; ++i)
        if (QString::fromUtf8(argv[i]) == QStringLiteral("-q"))
            wantQuality = QString::fromUtf8(argv[i + 1]);
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
        p->search(qFromUtf8("travel"), 1,
            [p, wantQuality, &pending, &failures, &done, pid](
                Result res, const QList<VideoItem>& items, bool) {
            if (res.code == Err::Unsupported) {
                // 能力不支持(如抖音仅热榜)= 跳过,不算失败
                std::printf("[skip] %-9s search: %s\n", qPrintable(pid),
                            qPrintable(res.message));
                done(); return;
            }
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
                p->detail(vid, [p, vid, pid, wantQuality, &pending, &failures, &done](
                                   Result res2, const VideoItem&,
                                   const QList<VideoItem>& parts) {
                    if (res2.code != Err::Ok || parts.isEmpty()) {
                        std::printf("[FAIL] %-9s detail %s: %s\n",
                                    qPrintable(pid), qPrintable(vid),
                                    qPrintable(res2.message));
                        ++failures;
                        done(); return;
                    } else {
                        auto onResolved = [pid, &pending, &failures, &done](
                                              Result res3, const PlayResult& pr) {
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
                                if (pid == qFromUtf8("youtube"))
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
                        };
                        if (wantQuality.isEmpty())
                            p->resolve(vid, onResolved);
                        else
                            p->resolveQuality(vid, wantQuality, onResolved);
                    }
                    Q_UNUSED(p);
                });
                return;   // pending 在 resolve 完成时才减
            }
            done();
        });
    }
    if (pending == 0) {   // 全部同步失败/跳过(如仅 Unsupported 的 provider)
        for (VideoProvider* p : provs) delete p;
        return failures == 0 ? 0 : 1;
    }
    QTimer::singleShot(90000, &app, []() {
        std::printf("[TIMEOUT] 90s\n");
        QCoreApplication::exit(2);
    });
    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    return rc;
}
