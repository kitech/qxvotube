// 小红书免登录链路验收(需外网):
//   ① search → 必须 Err::NeedAuth(无免登录通道)
//   ② homeFeed → 取视频笔记 → resolve(noteId@token) → 取流头 1MB
//   ③ userVideos → 元数据(可能无 id,见调研文档)
// 用法:./xhs [uid]   (节流:每请求均值 6s,整体约 20-30s)
#include "common/provider.h"
#include "common/debuglog.h"

#include "httplib.h"

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

using namespace qxv;

namespace {
int g_fail = 0;
void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qToUtf8(got).data()); }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    const QList<VideoProvider*> provs = createProviders();
    VideoProvider* xhs = nullptr;
    for (VideoProvider* p : provs)
        if (p->id() == QString::fromUtf8("xiaohongshu")) xhs = p;
    if (!xhs) { std::printf("[FAIL] no xiaohongshu provider\n"); return 1; }

    // ① 搜索必须 NeedAuth
    xhs->search(QString::fromUtf8("旅行"), 1,
        [](Result r, const QList<VideoItem>&, bool) {
        check(r.code == Err::NeedAuth, "search → NeedAuth", r.message);
    });

    // ② homeFeed → resolve
    xhs->homeFeed(1, [xhs](Result r, const QList<VideoItem>& items, bool hasMore) {
        check(r.code == Err::Ok && items.size() >= 3, "homeFeed items>=3",
              r.message + QString::fromUtf8(" n=") + QString::number(items.size()));
        std::printf("[info] hasMore=%d\n", int(hasMore));
        if (r.code != Err::Ok) { QCoreApplication::exit(1); return; }

        // 找一个视频笔记(带 token)
        QString target, token, uid;
        for (const VideoItem& it : items) {
            const QString t = it.extra.value(QString::fromUtf8("xsecToken")).toString();
            const QString type = it.extra.value(QString::fromUtf8("type")).toString();
            if (!uid.isEmpty() == false) uid = it.uid;
            if (type == QString::fromUtf8("video") && !t.isEmpty()) {
                target = it.id; token = t;
                break;
            }
        }
        if (target.isEmpty()) {
            check(false, "homeFeed 内找到视频笔记(带 token)");
            QCoreApplication::exit(1);
            return;
        }
        std::printf("[info] video note: %s\n", qToUtf8(target).data());

        const QString ref = target + QString::fromUtf8("@") + token;
        xhs->resolve(ref, [xhs, uid](Result r2, const PlayResult& pr) {
            check(r2.code == Err::Ok && !pr.streams.isEmpty(), "resolve ok",
                  r2.message);
            if (r2.code != Err::Ok || pr.streams.isEmpty()) {
                QCoreApplication::exit(1);
                return;
            }
            const Stream& s = pr.streams.first();
            std::printf("[info] stream %s %s\n", qToUtf8(s.quality).data(),
                        qPrintable(s.url.left(70)));
            check(s.url.startsWith(QString::fromUtf8("https://")), "https 直链");

            // 注意:Client(host,port) 恒明文;https 必须 scheme 形式
            const int sp = qIndexOf(s.url, QString::fromUtf8("://"));
            const int slash = s.url.indexOf(QLatin1Char('/'), sp + 3);
            httplib::Client cli(
                s.url.left(slash).toStdString());
            cli.set_follow_location(true);
            cli.set_read_timeout(30, 0);
            httplib::Headers h = { { "Range", "bytes=0-1048575" },
                                   { "Referer", "https://www.xiaohongshu.com/" } };
            const std::string path = s.url.mid(slash).toStdString();
            auto res = cli.Get(path, h);
            check(res && (res->status == 206 || res->status == 200)
                      && res->body.size() > 1024,
                  "流头 1MB", res ? QString::number(res->body.size()) : QString());
            if (res && res->body.size() > 12) {
                const QByteArray head(res->body.data(), 12);
                std::printf("[info] head: %.12s\n", head.constData());
            }

            // ③ userVideos(若 feed 里拿到 uid)
            if (!uid.isEmpty()) {
                xhs->userVideos(uid, 1,
                    [](Result r3, const QList<VideoItem>& v, bool) {
                    // 用户页 SSR 在本 IP 上常被软风控(37KB 壳)→ 记 skip
                    if (r3.code == Err::RateLimit || r3.code == Err::CaptchaRequired) {
                        std::printf("[skip] userVideos: 软风控(%s)\n",
                                    qToUtf8(r3.message).data());
                    } else {
                        check(r3.code == Err::Ok && !v.isEmpty(),
                              "userVideos 元数据", r3.message);
                    }
                    std::printf("xhs: %s\n", g_fail ? "FAILED" : "all green");
                    QCoreApplication::exit(g_fail ? 1 : 0);
                });
            } else {
                std::printf("xhs: %s\n", g_fail ? "FAILED" : "all green");
                QCoreApplication::exit(g_fail ? 1 : 0);
            }
        });
    });

    QTimer::singleShot(240000, &app, []() {
        std::printf("[TIMEOUT]\n");
        QCoreApplication::exit(2);
    });
    return app.exec();
}
