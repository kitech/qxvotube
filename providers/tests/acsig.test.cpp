// 离线单测:抖音 __ac_signature 纯算 golden + Argus md5 向量(不联网)
//   golden 来源:CSDN 145899104(2025-02 逆向还原,附样例)
//   md5 向量:python hashlib 独立核对(2026-11-18)
#include "douyin/acsig.h"

#include <QCoreApplication>
#include <QString>
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
    Q_UNUSED(app);

    // ── __ac_signature golden(CSDN 145899104 样例) ────────────────
    {
        const QString sig = douyin::acSignature(
            1740635781, QString::fromUtf8("www.douyin.com/"),
            QString::fromUtf8("067bffadf00143f576ddf"),
            QString::fromUtf8(
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36 "
                "Edg/133.0.0.0"));
        check(sig == QString::fromUtf8(
                  "_02B4Z6wo00f01HBw-fgAAIDA-rdLmXfuMxxwUP1AAHurb5"),
              "acSignature CSDN golden", sig);
        check(sig.size() == 47, "acSignature 长度 47",
              QString::number(sig.size()));
    }

    // ── Argus:md5(uifid_ts_SALT_query) ──────────────────────────
    {
        const QString m1 = douyin::argusSignature(
            QString::fromUtf8("abc"), QString(), QString::fromUtf8("x"));
        check(m1 == QString::fromUtf8("30904e1b5369a4a6d6889d86a4bfd245"),
              "argusSignature 向量1", m1);
        const QString m2 = douyin::argusSignature(
            QString::fromUtf8("uifid"), QString::fromUtf8("1794"),
            QString::fromUtf8("a=1&b=2"));
        check(m2 == QString::fromUtf8("70d8538608ee04459af7786530ac1814"),
              "argusSignature 向量2", m2);
    }

    if (g_fail == 0) std::printf("ALL PASS\n");
    return g_fail == 0 ? 0 : 1;
}
