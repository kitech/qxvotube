#include "acsig.h"

#include <string.h>

namespace qxv {
namespace douyin {
namespace {

// Qt3 无 u32(只有 Q_UINT32),本文件内固定 32 位无符号
typedef unsigned int u32;

// JS: for (c of s) k = ((k ^ c.charCodeAt(i)) * 65599) >>> 0
u32 cal1(const std::string& s, u32 k) {
    for (size_t i = 0; i < s.size(); ++i)
        k = ((k ^ (u32)(unsigned char)s[i]) * 65599u);
    return k;
}

// JS: k = (k * 65599 + s.charCodeAt(i)) >>> 0
u32 cal3(const std::string& s, u32 k) {
    for (size_t i = 0; i < s.size(); ++i)
        k = k * 65599u + (u32)(unsigned char)s[i];
    return k;
}

// JS get_one_chr: 26 大写 + 26 小写 + 10 数字 + 2 特殊('-', '.')
QChar oneChr(u32 n) {
    if (n < 26) return QChar('A' + (int)n);
    if (n < 52) return QChar('a' + (int)(n - 26));
    if (n < 62) return QChar('0' + (int)(n - 52));
    return QChar((n == 62) ? '-' : '.');
}

// 5 字符一组,从高 6 位到低 6 位(24,18,...,0)
QString encNum(quint64 v) {
    QString s;
    for (int i = 24; i >= 0; i -= 6) s += oneChr((u32)((v >> i) & 63));
    return s;
}

QString dec2bin32(u32 v) {
    QString s;
    for (int i = 31; i >= 0; --i) s += ((v >> i) & 1) ? QChar('1') : QChar('0');
    return s;
}

QString toHex2(u32 v) {
    static const char* d = "0123456789abcdef";
    QString s;
    s += QChar(d[(v >> 4) & 15]);
    s += QChar(d[v & 15]);
    return s;
}

// ── 自包含 MD5(避免 provider 依赖 qlcomp/OpenSSL) ──────────────
struct Md5Ctx {
    u32 a, b, c, d;
    quint64 len;
    unsigned char buf[64];
    int used;
};

static const u32 md5K[64] = {
    0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,
    0xa8304613u,0xfd469501u,0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,
    0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,0xf61e2562u,0xc040b340u,
    0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
    0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,
    0x676f02d9u,0x8d2a4c8au,0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,
    0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,0x289b7ec6u,0xeaa127fau,
    0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
    0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,
    0xffeff47du,0x85845dd1u,0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,
    0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u
};
static const int md5S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static u32 rol(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static void md5Block(Md5Ctx* c, const unsigned char* p) {
    u32 m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = (u32)p[i*4] | ((u32)p[i*4+1] << 8)
             | ((u32)p[i*4+2] << 16) | ((u32)p[i*4+3] << 24);
    u32 a=c->a,b=c->b,cc=c->c,d=c->d;
    for (int i = 0; i < 64; ++i) {
        u32 f; int g;
        if (i < 16)      { f = (b & cc) | (~b & d);        g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc);        g = (5*i+1) % 16; }
        else if (i < 48) { f = b ^ cc ^ d;                g = (3*i+5) % 16; }
        else             { f = cc ^ (b | ~d);             g = (7*i) % 16; }
        const u32 tmp = d;
        d = cc; cc = b;
        b = b + rol(a + f + md5K[i] + m[g], md5S[i]);
        a = tmp;
    }
    c->a += a; c->b += b; c->c += cc; c->d += d;
}

static QString md5Hex(const std::string& s) {
    Md5Ctx c;
    c.a = 0x67452301u; c.b = 0xefcdab89u;
    c.c = 0x98badcfeu; c.d = 0x10325476u;
    c.len = 0; c.used = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        c.buf[c.used++] = (unsigned char)s[i];
        if (c.used == 64) { md5Block(&c, c.buf); c.used = 0; }
        c.len += 8;
    }
    quint64 bits = c.len;
    c.buf[c.used++] = 0x80;
    if (c.used > 56) {
        while (c.used < 64) c.buf[c.used++] = 0;
        md5Block(&c, c.buf); c.used = 0;
    }
    while (c.used < 56) c.buf[c.used++] = 0;
    for (int i = 0; i < 8; ++i) c.buf[56+i] = (unsigned char)(bits >> (8*i));
    md5Block(&c, c.buf);
    QString out;
    const u32 xs[4] = {c.a, c.b, c.c, c.d};
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 4; ++i)
            out += toHex2((u32)((xs[k] >> (8*i)) & 0xff));
    return out;
}

} // namespace

QString acSignature(long long timeSec, const QString& site,
                    const QString& nonce, const QString& ua) {
    const std::string ss = qToUtf8(site).data();
    const std::string ns = qToUtf8(nonce).data();
    const std::string us = qToUtf8(ua).data();
    const quint64 ts = (quint64)timeSec;
    const quint64 tsq = (quint64)(cal1(std::string(qToUtf8(QString::number(ts)).data()), 0));
    const u32 a = (u32)(cal1(ss, (u32)tsq) % 65521u);
    // JS: "10000000110000" + parseInt((ts ^ (a*65521))>>>0).toString(2).padStart(32,'0')
    const QString bits = qFromUtf8("10000000110000")
                       + dec2bin32((u32)(ts ^ (quint64)(a * 65521u)));
    // 46 位大整数 → 用 quint64 承载(10^14 量级,远未溢出)
    quint64 b = 0;
    for (int i = 0; i < bits.length(); ++i)
        b = b * 2 + (quint64)(bits[i] == QChar('1') ? 1 : 0);
    const u32 c = cal1(std::string(qToUtf8(QString::number(b)).data()), 0);
    const QString d = encNum(b >> 2);
    const quint64 e = b / 4294967296ULL;
    const QString f = encNum(((b << 28) & 0xFFFFFFFFULL) | (e >> 4));
    const u32 g = (u32)(582085784u ^ (u32)b);
    const QString h = encNum(((e << 26) & 0xFFFFFFFFULL) | (g >> 6));
    const QChar i = oneChr(g & 63);
    const u32 j = ((cal1(us, c) % 65521u) << 16)
                    | (cal1(ns, c) % 65521u);
    const QString k = encNum(j >> 2);
    const QString l = encNum(((j << 28) & 0xFFFFFFFFULL)
                             | ((524576u ^ (u32)b) >> 4));
    const QString m = encNum(a);
    const QString n = qFromUtf8("_02B4Z6wo00f01") + d + f + h + i + k + l + m;
    const std::string ns2 = qToUtf8(n).data();
    return n + toHex2(cal3(ns2, 0));
}

QString argusSignature(const QString& uifid, const QString& timestamp,
                       const QString& query) {
    static const char* kSalt = "A96D855A08C0A9707F8BEF0D9A527E4E";
    const std::string raw = std::string(qToUtf8(uifid).data()) + "_"
                          + std::string(qToUtf8(timestamp).data()) + "_"
                          + kSalt + "_"
                          + std::string(qToUtf8(query).data());
    return md5Hex(raw);
}

} // namespace douyin
} // namespace qxv
