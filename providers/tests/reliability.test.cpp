// 可靠性三件验收(纯本地,无需外网):
//   ① NetClient 重试:本地 server 前 2 次 500,第 3 次 200 → maxAttempts=3 成功
//   ② cancel:对慢 server 发请求后 cancel → 回调不再触发
//   ③ streamcache:TTL 过期 + URL expire= 解析
// 用法:./reliability
#include "common/netclient.h"
#include "common/streamcache.h"
#include "common/ratelimit.h"
#include "common/debuglog.h"

#include "httplib.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <thread>

using namespace qxv;

namespace {
int g_fail = 0;

void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qPrintable(got)); }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    std::atomic<int> hits{ 0 };
    httplib::Server srv;
    srv.Get("/flaky", [&hits](const httplib::Request&, httplib::Response& res) {
        const int n = ++hits;
        if (n < 3) { res.status = 500; res.set_content("boom", "text/plain"); }
        else { res.status = 200; res.set_content("ok", "text/plain"); }
    });
    std::atomic<int> onceHits{ 0 };
    srv.Get("/once", [&onceHits](const httplib::Request&, httplib::Response& res) {
        ++onceHits;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        res.status = 200;
        res.set_content("once", "text/plain");
    });
    srv.Get("/slow", [](const httplib::Request&, httplib::Response& res) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        res.status = 200;
        res.set_content("late", "text/plain");
    });
    const int port = srv.bind_to_any_port("127.0.0.1");
    std::thread th([&srv] { srv.listen_after_bind(); });
    check(port > 0, "local server bound");

    // ── ① 重试 ──────────────────────────────────────────────
    int attemptResp = -1;
    int hitsAfter = 0;
    {
        NetRequest req;
        req.url = QString::fromUtf8("http://127.0.0.1:%1/flaky").arg(port);
        req.maxAttempts = 3;
        req.retryDelayMs = 50;
        static NetClient net;
        net.send(req, [&attemptResp, &hits, &hitsAfter](const NetResponse& r) {
            attemptResp = r.status;
            hitsAfter = hits.load();
            check(r.status == 200 && r.body == QByteArray("ok"),
                  "retry: 前两次 500 → 第三次成功", QString::number(r.status));
            check(hitsAfter == 3, "retry: 实际请求 3 次",
                  QString::number(hitsAfter));
        });
    }

    // ── ② cancel ────────────────────────────────────────────
    std::atomic<bool> cancelledCb{ false };
    static NetClient slowNet;
    {
        NetRequest req;
        req.url = QString::fromUtf8("http://127.0.0.1:%1/slow").arg(port);
        req.timeoutSec = 5;
        slowNet.send(req, [&cancelledCb](const NetResponse&) {
            cancelledCb = true;
        });
        QTimer::singleShot(100, &app, []() { slowNet.cancel(); });
    }

    // ── ③ streamcache ───────────────────────────────────────
    {
        streamcache::Cache c;
        PlayResult pr;
        Stream st;
        st.url = QString::fromUtf8("https://x/y?expire=99999999999&a=1");
        pr.streams.append(st);
        c.put(QString::fromUtf8("k"), pr, 300);
        PlayResult out;
        check(c.get(QString::fromUtf8("k"), &out) && !out.streams.isEmpty(),
              "cache: 未过期命中");
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(!c.get(QString::fromUtf8("k"), &out), "cache: TTL 过期后失效");

        const qint64 ttl = streamcache::ttlFromUrl(
            QString::fromUtf8("https://r.googlevideo.com/v?expire=99999999999"),
            10 * 60 * 1000);
        check(ttl > 0 && ttl <= 10 * 60 * 1000, "cache: 解析 expire= 并取 min",
              QString::number(ttl));
        const qint64 ttl2 = streamcache::ttlFromUrl(
            QString::fromUtf8("https://r/x?expire=1000"), 60000);
        check(ttl2 == 0, "cache: 已过期 URL 返回 0", QString::number(ttl2));
    }

    // ── ④ 单飞:3 个同 URL 并发 → server 只命中 1 次 ─────────
    {
        // shared_ptr:QTimer 回调晚于本作用域,不能按引用捕获局部原子量
        auto got = std::make_shared<std::atomic<int>>(0);
        static NetClient a, b2, c;
        const QString url = QString::fromUtf8("http://127.0.0.1:%1/once").arg(port);
        NetRequest req;
        req.url = url;
        auto cbk = [got](const NetResponse& r) {
            if (r.status == 200 && r.body == QByteArray("once")) ++(*got);
        };
        a.send(req, cbk);
        b2.send(req, cbk);
        c.send(req, cbk);
        QTimer::singleShot(700, &app, [got, &onceHits]() {
            check(got->load() == 3, "singleflight: 3 个等待者都收到响应",
                  QString::number(got->load()));
            check(onceHits.load() == 1, "singleflight: 服务端只命中 1 次",
                  QString::number(onceHits.load()));
        });
    }

    // ── ⑤ 限流:QPS=10 → 3 次同 host 至少 200ms ────────────────
    {
        RateLimiter::configure(10);
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 3; ++i) {
            NetRequest req;
            req.url = QString::fromUtf8("http://127.0.0.1:%1/slow").arg(port);
            req.singleFlight = false;
            req.timeoutSec = 5;
            static NetClient rl;
            rl.send(req, [](const NetResponse&) {});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // 直接在测试线程量 acquire 间隔(避免慢接口本身 1.5s 干扰)
        QElapsedTimer t2;
        t2.start();
        for (int i = 0; i < 3; ++i) RateLimiter::acquire(QString::fromUtf8("host-x"));
        const qint64 ms = t2.elapsed();
        check(ms >= 180, "ratelimit: 3 次同 host 间隔 >=180ms",
              QString::number(ms));
        RateLimiter::configure(0);   // 关掉,避免影响后续
    }

    // ── ⑥ 单飞等待者被 cancel:只丢自己,不影响 leader ──────────
    auto w1 = std::make_shared<std::atomic<bool>>(false);
    auto w2 = std::make_shared<std::atomic<bool>>(false);
    {
        static NetClient wa, wb;
        NetRequest rq;
        rq.url = QString::fromUtf8("http://127.0.0.1:%1/slow").arg(port);
        rq.timeoutSec = 5;
        wa.send(rq, [w1](const NetResponse&) { w1->store(true); });
        wb.send(rq, [w2](const NetResponse&) { w2->store(true); });
        QTimer::singleShot(100, &app, []() { wa.cancel(); });
    }

    // 等 cancel 场景走完(1.8s > slow 1.5s)
    QTimer::singleShot(1800, &app, [&cancelledCb, &srv, w1, w2]() {
        check(!cancelledCb.load(), "cancel: 慢请求回调被丢弃");
        check(!w1->load() && w2->load(),
              "cancel: 单飞等待者只丢自己(leader 正常收到)");
        srv.stop();
        QCoreApplication::exit(g_fail ? 1 : 0);
    });
    QTimer::singleShot(30000, &app, []() {
        std::printf("[TIMEOUT]\n");
        QCoreApplication::exit(2);
    });

    const int rc = app.exec();
    th.join();
    std::printf("reliability: %s\n", g_fail ? "FAILED" : "all green");
    return rc;
}
