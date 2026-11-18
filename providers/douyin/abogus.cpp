// 抖音 a_bogus —— C++ 移植
// 来源:ylcangel/douyin_sign a_bogus(utils.js + sm3.js, Apache-2.0, AngelToms)
// 对拍:node 跑参考 JS(确定性 LCG + 固定 Date.now + 固定 enterPageTs) vs 本实现,
//       5 组用例(不同 seed/now/uri/ua/窗口指纹)字节级一致(abogus_golden_test.cpp)
// 边界:web 口径 sdkVersion=1.0.1.19-fix.01;签名含随机成分,服务端按统计校验;
//       上游换版本需重新对拍(docs/providers/protocols/bytedance.md 记录)。
#include "abogus.h"
#include <stdio.h>
#include <string.h>
#include <cstdint>
#include <vector>

namespace qxv { namespace abogus {

static const char* kSalt = "dhzx";

// 窗口指纹(U[76]):Windows Chrome 1920x1080 口径。格式:
//   innerWidth|innerHeight|outerWidth|outerHeight|availWidth|availHeight|
//   sizeWidth|sizeHeight|platform(sizeWidth/sizeHeight 恒 0,参考 harness 同款)
// 服务端按统计口径校验,不校验具体值;写死即可。
static const char* kWindowFp = "1920|1080|1920|1040|1920|1040|0|0|Win32";

// ── SM3(GB/T 32905-2016;与参考 JS 同实现,大端输出) ───────────────
static uint32_t rotl32(uint32_t x, int n) {
    n %= 32;
    return n == 0 ? x : (x << n) | (x >> (32 - n));
}
struct Sm3 {
    uint32_t reg[8];
    unsigned char chunk[128];
    int chunkLen;
    long long size;
    Sm3() {
        reg[0]=0x7380166f; reg[1]=0x4914b2b9; reg[2]=0x172442d7; reg[3]=0xda8a0600;
        reg[4]=0xa96f30bc; reg[5]=0x163138aa; reg[6]=0xe38dee4d; reg[7]=0xb0fb0e4e;
        chunkLen = 0; size = 0;
    }
    void compress(const unsigned char* m) {
        uint32_t w[132];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)m[i*4]<<24)|((uint32_t)m[i*4+1]<<16)|
                   ((uint32_t)m[i*4+2]<<8)|(uint32_t)m[i*4+3];
        for (int j = 16; j < 68; ++j) {
            uint32_t x = w[j-16] ^ w[j-9] ^ rotl32(w[j-3],15);
            x = x ^ rotl32(x,15) ^ rotl32(x,23);
            w[j] = x ^ rotl32(w[j-13],7) ^ w[j-6];
        }
        for (int j = 0; j < 64; ++j)
            w[j+68] = w[j] ^ w[j+4];
        uint32_t r[8];
        for (int i = 0; i < 8; ++i) r[i] = reg[i];
        for (int j = 0; j < 64; ++j) {
            uint32_t ss1 = rotl32(r[0],12) + r[4] + rotl32(j<16?0x79cc4519u:0x7a879d8au, j);
            ss1 = rotl32(ss1,7);
            uint32_t ss2 = ss1 ^ rotl32(r[0],12);
            uint32_t tt1 = (j<16 ? (r[0]^r[1]^r[2])
                                 : ((r[0]&r[1])|(r[0]&r[2])|(r[1]&r[2])))
                          + r[3] + ss2 + w[j+68];
            uint32_t tt2 = (j<16 ? (r[4]^r[5]^r[6]) : ((r[4]&r[5])|(~r[4]&r[6])))
                          + r[7] + ss1 + w[j];
            r[3]=r[2]; r[2]=rotl32(r[1],9); r[1]=r[0]; r[0]=tt1;
            r[7]=r[6]; r[6]=rotl32(r[5],19); r[5]=r[4];
            r[4]=tt2 ^ rotl32(tt2,9) ^ rotl32(tt2,17);
        }
        for (int i = 0; i < 8; ++i) reg[i] ^= r[i];
    }
    void write(const unsigned char* m, int len) {
        size += len;
        int i = 64 - chunkLen;
        if (len < i) { memcpy(chunk+chunkLen, m, len); chunkLen += len; return; }
        memcpy(chunk+chunkLen, m, i); chunkLen += i;
        compress(chunk);
        while (len - i >= 64) { compress(m+i); i += 64; }
        chunkLen = 0;
        if (len > i) { memcpy(chunk, m+i, len-i); chunkLen = len-i; }
    }
    void digest(unsigned char out[32]) {
        fill();
        for (int i = 0; i < chunkLen; i += 64) compress(chunk+i);
        for (int i = 0; i < 8; ++i) {
            out[i*4]   = (unsigned char)(reg[i]>>24);
            out[i*4+1] = (unsigned char)(reg[i]>>16);
            out[i*4+2] = (unsigned char)(reg[i]>>8);
            out[i*4+3] = (unsigned char)(reg[i]);
        }
    }
    void fill() {
        long long l = size * 8;
        chunk[chunkLen++] = 0x80;
        int len = chunkLen % 64;
        if (64 - len < 8) len -= 64;
        while (len < 56) { chunk[chunkLen++] = 0; ++len; }
        for (int i = 0; i < 4; ++i)
            chunk[chunkLen++] = (unsigned char)((uint32_t)(l>>32) >> ((3-i)*8));
        for (int i = 0; i < 4; ++i)
            chunk[chunkLen++] = (unsigned char)((uint32_t)l >> ((3-i)*8));
    }
};
static void sm3Twice(const std::string& msg, unsigned char out[32]) {
    Sm3 a; a.write((const unsigned char*)msg.data(), (int)msg.size());
    unsigned char d1[32]; a.digest(d1);
    Sm3 b; b.write(d1, 32); b.digest(out);
}
static void sm3Once(const std::string& msg, unsigned char out[32]) {
    Sm3 a; a.write((const unsigned char*)msg.data(), (int)msg.size());
    a.digest(out);
}

// ── 魔改 RC4:S 初始化倒序(255-i) + KSA 权重 (j*S[i]+j+K[i]) ──────
static std::string dyRc4(const std::string& key, const std::string& text) {
    unsigned char S[256], K[256];
    for (int i = 0; i < 256; ++i) {
        S[i] = (unsigned char)(255 - i);
        K[i] = (unsigned char)key[i % key.size()];
    }
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j * S[i] + j + K[i]) % 256;
        unsigned char t = S[i]; S[i] = S[j]; S[j] = t;
    }
    int i2 = 0, k = 0;
    std::string out; out.reserve(text.size());
    for (int n = 0; n < (int)text.size(); ++n) {
        i2 = (i2 + 1) % 256;
        k = (k + S[i2]) % 256;
        unsigned char t = S[i2]; S[i2] = S[k]; S[k] = t;
        unsigned char rnd = S[(S[i2] + S[k]) % 256];
        out += (char)((unsigned char)text[n] ^ rnd);
    }
    return out;
}

// ── 魔改 base64(自定义表 s0..s4) ─────────────────────────────────
static const char* kLetter[5] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=",
    "Dkdpgh4ZKsQB80/Mfvw36XI1R25+WUAlEi7NLboqYTOPuzmFjJnryx9HVGcaStCe=",
    "Dkdpgh4ZKsQB80/Mfvw36XI1R25-WUAlEi7NLboqYTOPuzmFjJnryx9HVGcaStCe=",
    "ckdp1h4ZKsUB80/Mfvw36XIgR25+WQAlEi7NLboqYTOPuzmFjJnryx9HVGDaStCe",
    "Dkdpgh2ZmsQB80/MfvV36XI1R45-WUAlEixNLwoqYTOPuzKFjJnry79HbGcaStCe",
};
static std::string dyBase64(const std::string& in, int table) {
    const char* T = kLetter[table];
    std::string out;
    int i = 0;
    while (i < (int)in.size()) {
        bool h2 = i+1 < (int)in.size(), h3 = i+2 < (int)in.size();
        uint32_t c = ((uint32_t)(unsigned char)in[i] << 16)
                   | (h2 ? ((uint32_t)(unsigned char)in[i+1] << 8) : 0)
                   | (h3 ?  (uint32_t)(unsigned char)in[i+2] : 0);
        out += T[(c>>18)&63]; out += T[(c>>12)&63];
        out += h2 ? T[(c>>6)&63] : '=';
        out += h3 ? T[c&63]      : '=';
        i += 3;
    }
    return out;
}

// JS ToInt32(浮点按 C 整数截断同语义,值域一致)
static int toInt32(double d) { return (int32_t)d; }

// random():参考实现 0x55/0xAA 交织;flag=1→r2 恒 0(NaN>>0);flag=2→r1 独立随机
struct RandOut { unsigned char b[4]; };
static void doRandom(double (*rand)(void*), void* ctx,
                     int in0, int in1, int flag, RandOut* o) {
    int r1, r2;
    if (flag == 2) {
        rand(ctx);                    // 参考实现开头无条件 r=Math.random()*65535(被覆写弃用)
        r1 = toInt32(rand(ctx) * 240.0);
        if (r1 > 109) { r1 = r1 + (r1 % 2); r1 += 1; }
        r2 = 0;                       // Math.random(未调用)→NaN>>0=0
        r2 = (r2 & 77) | 2 | 16 | 32 | 128;   // =178
    } else {
        double r = rand(ctx) * 65535.0;
        r1 = toInt32(r) & 255;
        r2 = (toInt32(r) >> 8) & 255;
        if (flag == 1) r2 = 0;
    }
    o->b[0] = (unsigned char)((r1 & 0xAA) | (in0 & 0x55));
    o->b[1] = (unsigned char)((r1 & 0x55) | (in0 & 0xAA));
    o->b[2] = (unsigned char)((r2 & 0xAA) | (in1 & 0x55));
    o->b[3] = (unsigned char)((r2 & 0x55) | (in1 & 0xAA));
}

// u88Transform:3 字节组 → 4 字节(x 随机交织;魔数 145/110/66/189/44/211)
static void u88Transform(const std::vector<int>& in, std::vector<int>* out,
                         double (*rand)(void*), void* ctx) {
    size_t i = 0;
    const int E1=145, E2=110, E3=66, E4=189, E5=44, E6=211;
    while (i < in.size()) {
        if (i + 2 < in.size()) {
            int x = toInt32(rand(ctx) * 1000.0) & 255;
            out->push_back((x & E1) | (in[i] & E2));
            out->push_back((x & E3) | (in[i+1] & E4));
            out->push_back((x & E5) | (in[i+2] & E6));
            out->push_back((in[i] & E1) | (in[i+1] & E3) | (in[i+2] & E5));
        } else {
            out->push_back(in[i]);
            if (i + 1 < in.size()) out->push_back(in[i+1]);
        }
        i += 3;
    }
}

static std::string fromCodes(const std::vector<int>& v) {
    std::string s; s.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        s += (char)(unsigned char)(v[i] & 255);
    return s;
}

std::string generate(const std::string& uri, const std::string& ua,
                     long long nowMs, void* randCtx, double (*rand)(void*),
                     const char* windowFp) {
    // 随机成分(顺序必须与参考一致:inObj1 → inObj2 → U[89] → u88 逐组)
    RandOut u86a, u86b, u89r;
    doRandom(rand, randCtx, 1, 0, 0, &u86a);   // {0:[1,0],length:1}   flag=0
    doRandom(rand, randCtx, 1, 0, 2, &u86b);   // {0:[1,0],1:2,...}    flag=2
    doRandom(rand, randCtx, 3, 82, 1, &u89r);  // {0:[3,82],1:1,...}   flag=1

    const long long U14 = nowMs;
    const int U28 = (int)((U14 - nowMs + 3) & 255);   // enterPageTs=nowMs → 3
    const int U29 = (int)(U14 & 255);
    const int U30 = (int)((U14 >> 8) & 255);
    const int U31 = (int)((U14 >> 16) & 255);
    const int U32 = (int)((U14 >> 24) & 255);
    const long long U14hi = (long long)(U14 / 4294967296.0);
    const int U33 = (int)(U14hi & 255);
    const int U34 = (int)((U14hi / 256) & 255);
    const int U16 = 1, U17 = 14, U24 = 41, U12 = 3, U27 = 6;
    const int U35 = U16 % 256 & 255, U36 = (U16 / 256) & 255;
    const long long U26 = (nowMs - 1721836800000LL) / 1000 / 60 / 60 / 24 / 14;
    const int U37_4 = 129;                    // U[37]=[0,0,0,0,129](参考注释同款)
    const int U38 = U37_4 & 255, U39 = (U37_4 >> 8) & 255;
    const int U40 = 0, U41 = 0, U42 = 0, U43 = 0;
    const int U44 = U17 & 255, U45 = (U17>>8)&255, U46 = (U17>>16)&255, U47 = (U17>>24)&255;

    // UA/URI 三路摘要
    std::string uriSalt = uri;
    if (uriSalt.size() < 4 ||
        uriSalt.compare(uriSalt.size()-4, 4, kSalt) != 0)
        uriSalt += kSalt;
    unsigned char uSm[32], hSm[32], uaSm[32];
    sm3Twice(uriSalt, uSm);                    // U[18]
    sm3Twice(std::string(kSalt), hSm);         // U[19]
    std::string uaKey; uaKey += (char)0; uaKey += (char)1; uaKey += (char)14;
    std::string uaTrim = ua;
    while (!uaTrim.empty() && (uaTrim[0]==' '||uaTrim[0]=='\t')) uaTrim.erase(0,1);
    while (!uaTrim.empty() && (uaTrim[uaTrim.size()-1]==' '||uaTrim[uaTrim.size()-1]=='\t'))
        uaTrim.erase(uaTrim.size()-1);
    std::string uaEncode = dyBase64(dyRc4(uaKey, uaTrim), 3);   // U[20] "s3"
    sm3Once(uaEncode, uaSm);                   // U[21]
    const long long ink = nowMs - 1;           // U[22]
    const int U60=(int)(ink&255), U61=(int)((ink>>8)&255), U62=(int)((ink>>16)&255),
              U63=(int)((ink>>24)&255);
    const long long inkHi = (long long)(ink / 4294967296.0);
    const int U64=(int)(inkHi&255), U65=(int)((inkHi/256)&255);
    const int U48=uSm[9], U49=uSm[18], U51=uSm[3];
    const int U52=hSm[10], U53=hSm[19], U55=hSm[4];
    const int U56=uaSm[11], U57=uaSm[21], U59=uaSm[5];
    const int U66 = U12;
    const int kPageId = 7571, kAid = 6383;
    const int U67=kPageId&255, U68=(kPageId>>8)&255, U69=(kPageId>>16)&255, U70=(kPageId>>24)&255;
    const int U71=kAid&255, U72=(kAid>>8)&255, U73=(kAid>>16)&255, U74=(kAid>>24)&255;

    // 窗口指纹(U[76]):格式=参考定义序
    // innerWidth|innerHeight|outerWidth|outerHeight|availWidth|availHeight|sizeWidth|sizeHeight|platform
    // 调用方传 windowFp 可覆盖;缺省用 kWindowFp(Windows Chrome 口径,见文件头)。
    std::string winStr(windowFp ? windowFp : kWindowFp);
    std::vector<int> u77;
    for (size_t i = 0; i < winStr.size(); ++i) u77.push_back((unsigned char)winStr[i]);
    const int U78 = (int)u77.size();
    const int U79 = U78 & 255, U80 = (U78>>8) & 255;
    char u81buf[24];
    snprintf(u81buf, sizeof(u81buf), "%d,", (int)((U14 + 3) & 255));
    std::vector<int> u82;
    for (const char* p = u81buf; *p; ++p) u82.push_back((unsigned char)*p);
    const int U83 = (int)u82.size();
    const int U84 = U83 & 255, U85 = (U83>>8) & 255;

    // U[87]:参考公式逐项异或(跳过 U50/U54/U58)
    int U87 = 0;
    { const int t[] = { u86a.b[0],u86a.b[1],u86a.b[2],u86a.b[3],
                        u86b.b[0],u86b.b[1],u86b.b[2],u86b.b[3],
                        U24,(int)U26,U27,U28,U29,U30,U31,U32,U33,U34,U35,U36,
                        U38,U39,U40,U41,U42,U43,U44,U45,U46,U47,U48,U49,U51,
                        U52,U53,U55,U56,U57,U59,U60,U61,U62,U63,U64,U65,U66,
                        U67,U68,U69,U70,U71,U72,U73,U74,U79,U80,U84,U85 };
      for (int i = 0; i < (int)(sizeof(t)/sizeof(t[0])); ++i) U87 ^= t[i]; }

    // U[88] = tlist ++ U77 ++ U82 ++ [U87](tlist 顺序=参考 sort 注释)
    std::vector<int> u88;
    { const int tlist[] = { U34,U44,U56,U61,U73,U29,U70,U45,U35,U49,
                            U38,U66,U51,U68,U28,U48,U64,U47,U30,U71,
                            (int)U26,U55,U31,U69,U59,U40,U62,U63,U27,U72,
                            U41,U74,U57,U52,U42,U39,U33,U67,U53,U43,
                            U65,U46,U36,U24,U60,U32,U79,U80,U84,U85 };
      for (int i = 0; i < 50; ++i) u88.push_back(tlist[i]); }
    u88.insert(u88.end(), u77.begin(), u77.end());
    u88.insert(u88.end(), u82.begin(), u82.end());
    u88.push_back(U87);

    // U[89] → 4 字符头
    std::string u89s = fromCodes(std::vector<int>{ u89r.b[0],u89r.b[1],u89r.b[2],u89r.b[3] });
    std::vector<int> u90;
    u88Transform(u88, &u90, rand, randCtx);
    std::vector<int> t1;
    t1.push_back(u86a.b[0]); t1.push_back(u86a.b[1]); t1.push_back(u86a.b[2]); t1.push_back(u86a.b[3]);
    t1.push_back(u86b.b[0]); t1.push_back(u86b.b[1]); t1.push_back(u86b.b[2]); t1.push_back(u86b.b[3]);
    t1.insert(t1.end(), u90.begin(), u90.end());
    std::string nkey; nkey += (char)211;
    std::string u91 = dyRc4(nkey, fromCodes(t1));
    return dyBase64(u89s + u91, 4);            // "s4"
}


}} // namespace qxv::abogus
