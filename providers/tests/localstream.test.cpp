// localstream 通用性验收(离线,进 ctest):
//   内存 Source → HTTP Range/416/会话回收/空闲关/重启,与具体解密逻辑无关。
#include "common/localstream/localstream.h"
#include "common/debuglog.h"

#include "httplib.h"

#include <QCoreApplication>
#include <QThread>
#include <cstdio>

using namespace qxv;

namespace {

int g_fail = 0;

void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qToUtf8(got).data()); }
}

// 100KB 可预测内容:第 i 字节 = 'A' + i%26
class MemorySource : public localstream::Source {
public:
    explicit MemorySource(int n) {
        buf_.resize(n);
        for (int i = 0; i < n; ++i) buf_[i] = char('A' + i % 26);
    }
    uint64_t total() const override { return uint64_t(buf_.size()); }
    bool read(uint64_t off, uint64_t len, QByteArray* out) override {
        if (off >= uint64_t(buf_.size())) { out->clear(); return true; }
        const uint64_t avail = uint64_t(buf_.size()) - off;
        const uint64_t n = qMin<uint64_t>(len, avail);
        *out = buf_.mid(int(off), int(n));
        return true;
    }

private:
    QByteArray buf_;
};

struct HttpResult {
    int status = 0;
    QByteArray body;
    QString contentRange;
};

HttpResult get(int port, const QString& path, quint64 off, quint64 len) {
    HttpResult r;
    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(10, 0);
    httplib::Headers h = { { "Range", QString::fromUtf8("bytes=%1-%2")
                                        .arg(off).arg(off + len - 1)
                                        .toUtf8()
                                        .constData() } };
    auto res = cli.Get(path.toStdString(), h);
    if (!res) return r;
    r.status = res->status;
    r.body = QByteArray(res->body.data(), int(res->body.size()));
    r.contentRange = QString::fromUtf8(res->get_header_value("Content-Range").c_str());
    return r;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();

    auto& srv = localstream::Server::instance();
    localstream::Options o;
    o.idleSec = 6;
    o.sessionIdleSec = 2;
    o.maxSessions = 2;
    srv.configure(o);

    QString url;
    Result r = srv.open(QString::fromUtf8("mem"), std::make_shared<MemorySource>(100 * 1024), &url);
    check(r.code == Err::Ok, "open memory source", r.message);
    std::printf("[info] url: %s\n", qToUtf8(url).data());

    const int port = srv.port();
    const QString path = url.mid(qIndexOf(url, qFromUtf8("/"), 7));

    HttpResult a = get(port, path, 0, 1024);
    check(a.status == 206, "range 206", QString::number(a.status));
    check(a.body.size() == 1024, "range size", QString::number(a.body.size()));
    bool bytesOk = true;
    for (int i = 0; i < a.body.size(); ++i)
        if (a.body[i] != char('A' + i % 26)) bytesOk = false;
    check(bytesOk, "range content matches source");
    check(a.contentRange.startsWith(QString::fromUtf8("bytes 0-1023/")),
          "Content-Range head", a.contentRange);

    HttpResult b = get(port, path, 50000, 100);
    check(b.status == 206 && b.body.size() == 100, "range mid");
    check(!b.body.isEmpty() && b.body[0] == char('A' + 50000 % 26),
          "range mid content");

    HttpResult c = get(port, path, 100 * 1024 + 10, 10);
    check(c.status == 416, "range 416", QString::number(c.status));

    // 会话回收 + 空闲关(参数化短超时)
    srv.release(QString::fromUtf8("mem"));
    QThread::sleep(4);
    check(srv.stats().sessions == 0, "session evicted",
          QString::number(srv.stats().sessions));
    QThread::sleep(5);
    check(!srv.stats().running, "idle stopped");
    check(srv.stats().lastStopReason == QString::fromUtf8("idle"), "stop reason",
          srv.stats().lastStopReason);

    // 重启
    QString url2;
    Result r2 = srv.open(QString::fromUtf8("mem2"),
                         std::make_shared<MemorySource>(2048), &url2);
    check(r2.code == Err::Ok && srv.stats().running, "restarted", r2.message);
    if (r2.code == Err::Ok) {
        const QString p2 = url2.mid(qIndexOf(url2, qFromUtf8("/"), 7));
        HttpResult d = get(srv.port(), p2, 0, 64);
        check(d.status == 206 && d.body.size() == 64, "restarted serves");
        srv.release(QString::fromUtf8("mem2"));
    }
    srv.stop(QString::fromUtf8("test-end"));
    check(!srv.stats().running, "explicit stop");

    std::printf("localstream: %s\n", g_fail ? "FAILED" : "all green");
    return g_fail ? 1 : 0;
}
