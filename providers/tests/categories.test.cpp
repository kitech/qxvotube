// 动态分类验收(需外网;bilibili 需 QXVOTUBE_PROXY 视网络而定):
//   bilibili   categories → ≥2 项(popular 条目自带 tid/tname 派生)
//   hongguo    categories → ≥2 项(首页 SSR homeSections tab)
//   douyin     现状无免签分类维度 → Unsupported(不得返回假分类)
//   xiaohongshu/youtube  允许 Ok 或 Unsupported(风控/空壳时 Unsupported)
// 用法:./categories [providerId]
#include "common/provider.h"
#include "common/debuglog.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

using namespace qxv;

namespace {
int g_fail = 0;

void check(bool ok, const QString& what) {
    if (ok) std::printf("[ ok ] %s\n", qPrintable(what));
    else { ++g_fail; std::printf("[FAIL] %s\n", qPrintable(what)); }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QString only = argc > 1 ? QString::fromUtf8(argv[1])
                                  : qFromUtf8("all");
    QList<VideoProvider*> provs = createProviders();
    int pending = 0;

    for (VideoProvider* p : provs) {
        if (only != qFromUtf8("all") && p->id() != only) continue;
        ++pending;
        const QString pid = p->id();
        p->categories([pid, &pending](Result r,
                                      const QList<CategoryItem>& cats) {
            const QString head = QString::fromUtf8("[%1] ").arg(pid);
            QStringList names;
            for (int i = 0; i < cats.size() && i < 6; ++i)
                names.append(cats[i].name);
            if (pid == qFromUtf8("bilibili") || pid == qFromUtf8("hongguo")) {
                check(r.code == Err::Ok && cats.size() >= 2,
                      head + QString::fromUtf8("%1 项 (keys=%2)")
                                 .arg(cats.size())
                                 .arg(names.join(QString::fromUtf8(","))));
            } else if (pid == qFromUtf8("douyin")) {
                check(r.code == Err::Unsupported,
                      head + QString::fromUtf8("无免签分类 → Unsupported (%1)")
                                 .arg(r.message.left(40)));
            } else {
                // 风控/空壳/网络不可达:允许失败,但不允许"假分类"
                check(r.code == Err::Ok || r.code == Err::Unsupported
                          || r.code == Err::Network,
                      head + (r.code == Err::Ok
                                  ? QString::fromUtf8("%1 项 (%2)")
                                        .arg(cats.size())
                                        .arg(names.join(QString::fromUtf8(",")))
                                  : QString::fromUtf8("→ %1 (%2)，仅推荐")
                                        .arg(int(r.code))
                                        .arg(r.message.left(30))));
            }
            --pending;
        });
    }

    QTimer::singleShot(120000, &app, []() {
        std::printf("[TIMEOUT] 120s\n");
        QCoreApplication::exit(2);
    });
    QTimer* poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, &app, [&pending]() {
        if (pending == 0) QCoreApplication::exit(g_fail ? 1 : 0);
    });
    poll->start(200);

    const int rc = app.exec();
    for (VideoProvider* p : provs) delete p;
    std::printf("categories: %s\n", g_fail ? "FAILED" : "all green");
    return rc;
}
