// 小红书签名器三层离线验收(不联网、零 Qt 依赖;Qt3/Qt6 同一份源码):
//   ① KAT:MD5(RFC1321)/RC4(RFC6229)/CRC32(0xCBF43926 + x9 变换)
//   ② 差分对拍:1000 组 fixture(tests/fixtures/xhssign.json,由
//      scripts/gen_xhssign_fixture.py 用 Python xhshow 生成;同种子序列
//      同时间戳,逐组比对 x-s / x-s-common 全串)
//   ③ 结构自证:解自产 x-s → 主表反解 → B64 decode → JSON 形状/x3 前缀/
//      x3 段 144 字节;x-s-common 的 x5/x8/x9 与重算一致
//
// SeqRng 消费序(与 fixture 生成脚本注入序一一对应):
//   signXs:      [seed32, offset(10..50), seq(15..50), wpl(1000..1200)]
//   signXsCommon: [x36(1..20)]
//
// 用法: xhssign [fixture 目录]   默认 tests/fixtures
#include "xiaohongshu/sign.h"
#include "cJSON.h"

#include "../../qlcomp/md5.h"
extern "C" {
#include "../../vendor/wjcryptlib/WjCryptLib_Rc4.h"
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using qxv::xhssign::RandomSource;
using qxv::xhssign::Signer;

static int g_fail = 0;
static void check(bool ok, const char* what, const std::string& got = "") {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, got.c_str()); }
}

// 固定序列随机源(逐个消费,超出后回绕)
namespace {
struct SeqRng : RandomSource {
    std::vector<uint32_t> seq;
    size_t i;
    SeqRng() : i(0) {}
    uint32_t next() { return seq.empty() ? 0u : seq[i++ % seq.size()]; }
    uint32_t seed32() { return next(); }
    int intInRange(int lo, int hi) {
        return lo + (int)(next() % (uint32_t)(hi - lo + 1));
    }
};
} // namespace

// ── 反解工具(结构自证用)─────────────────────────────────────
static const char* kStdB64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string remapBack(const std::string& s, const std::string& dstTab) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '=') { out.push_back('='); continue; }
        const size_t idx = dstTab.find(c);
        out.push_back(idx == std::string::npos ? c : kStdB64[idx]);
    }
    return out;
}

// 极简标准 base64 解码(自证用,不引第三方)
static std::string b64Decode(const std::string& in) {
    int rev[256];
    for (int i = 0; i < 256; ++i) rev[i] = -1;
    for (int i = 0; i < 64; ++i) rev[(unsigned char)kStdB64[i]] = i;
    int buf = 0, bits = 0;
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '=') break;
        if (rev[c] < 0) continue;
        buf = (buf << 6) | rev[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xff));
        }
    }
    return out;
}

static bool findStr(const std::string& json, const char* key, std::string* out) {
    const std::string pat = std::string("\"") + key + "\":\"";
    const size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    const size_t q = json.find('"', p + pat.size());
    if (q == std::string::npos) return false;
    *out = json.substr(p + pat.size(), q - (p + pat.size()));
    return true;
}

// cJSON 取字段(字符串/数字/数组)
static std::string jstrOf(const cJSON* o, const char* key) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : 0;
    return (cJSON_IsString(it) && it->valuestring) ? std::string(it->valuestring)
                                                   : std::string();
}
static long long jintOf(const cJSON* o, const char* key, long long def = 0) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : 0;
    return cJSON_IsNumber(it) ? (long long)it->valuedouble : def;
}
static const cJSON* jarrOf(const cJSON* o, const char* key) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : 0;
    return cJSON_IsArray(it) ? it : 0;
}

int main(int argc, char** argv) {
    std::string fixtureDir = "tests/fixtures";
    for (int i = 1; i < argc; ++i) fixtureDir = argv[i];

    // ── ① KAT(原语级,独立于 fixture)─────────────────────
    {
        struct { const char* in; const char* out; } mv[] = {
            {"", "d41d8cd98f00b204e9800998ecf8427e"},
            {"abc", "900150983cd24fb0d6963f7d28e17f72"},
            {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        };
        int ok = 0;
        for (unsigned i = 0; i < sizeof(mv) / sizeof(mv[0]); ++i) {
            MD5_CTX c; MD5_Init(&c);
            MD5_Update(&c, (const uint8_t*)mv[i].in, strlen(mv[i].in));
            uint8_t d[16]; MD5_Final(d, &c);
            char hex[33] = {0};
            for (int k = 0; k < 16; ++k) std::sprintf(hex + 2 * k, "%02x", d[k]);
            if (strcmp(hex, mv[i].out) == 0) ++ok;
        }
        check(ok == 3, "KAT MD5(RFC1321 3 向量)");
    }
    {
        const uint8_t key[5] = {1, 2, 3, 4, 5};
        const uint8_t zeros[16] = {0};
        uint8_t out[16];
        Rc4XorWithKey(key, 5, 0, zeros, out, 16);
        const uint8_t expect[16] = {0xb2,0x39,0x63,0x05,0xf0,0x3d,0xc0,0x27,
                                    0xcc,0xc3,0x52,0x4a,0x0a,0x11,0x18,0xa8};
        check(memcmp(out, expect, 16) == 0, "KAT RC4(RFC6229)");
    }
    {
        const std::string s9 = "123456789";
        const uint32_t v = (uint32_t)::crc32(0, (const Bytef*)s9.data(),
                                             (uInt)s9.size());
        check(v == 0xCBF43926u, "KAT CRC32(0xCBF43926)");
        check((int)(v ^ 0xEDB88320u) == (int)(0xCBF43926u ^ 0xEDB88320u),
              "KAT x9 变换(POLY 异或)");
    }

    // ── ② 差分对拍(1000 组)─────────────────────────────────
    std::ifstream fin((fixtureDir + "/xhssign.json").c_str(),
                      std::ios::in | std::ios::binary);
    if (!fin) {
        std::printf("[FAIL] fixture 打不开: %s\n",
                    (fixtureDir + "/xhssign.json").c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << fin.rdbuf();
    const std::string fx = ss.str();

    cJSON* root = cJSON_ParseWithLength(fx.data(), fx.size());
    check(root != 0, "fixture JSON 解析");
    if (!root) { std::printf("xhssign: FAILED\n"); return 1; }
    const cJSON* cases = jarrOf(root, "cases");
    const int n = cases ? cJSON_GetArraySize(const_cast<cJSON*>(cases)) : 0;
    check(n == 1000, "fixture 1000 组", std::to_string(n));

    SeqRng rng;
    Signer s(qxv::xhssign::SignConfig(), &rng);
    int okXs = 0, okXsc = 0;
    std::string firstBad;
    const long long tsMs = jintOf(root, "tsMs");
    for (int i = 0; i < n; ++i) {
        const cJSON* c = cJSON_GetArrayItem(const_cast<cJSON*>(cases), i);
        const std::string method = jstrOf(c, "method");
        const std::string uri = jstrOf(c, "uri");
        const std::string a1 = jstrOf(c, "a1");
        // payload:GET 为 null → "";POST 为对象 → 紧凑 JSON(cJSON 保插入序,
        // 与 Python json.dumps(separators=(",",":")) 同序)
        std::string payload;
        {
            const cJSON* pit = cJSON_GetObjectItemCaseSensitive(c, "payload");
            if (cJSON_IsString(pit)) payload = jstrOf(c, "payload");
            else if (pit && !cJSON_IsNull(pit)) {
                char* js = cJSON_PrintUnformatted(const_cast<cJSON*>(pit));
                payload = js ? js : "";
                if (js) cJSON_free(js);
            }
        }
        const std::string expectXs = jstrOf(c, "expectXs");
        const std::string expectXsc = jstrOf(c, "expectXsCommon");

        rng.seq.clear();
        rng.i = 0;
        const cJSON* seqA = jarrOf(c, "seq");
        const int sn = seqA ? cJSON_GetArraySize(const_cast<cJSON*>(seqA)) : 0;
        for (int k = 0; k < sn; ++k) {
            const cJSON* it = cJSON_GetArrayItem(const_cast<cJSON*>(seqA), k);
            rng.seq.push_back(cJSON_IsNumber(it) ? (uint32_t)it->valuedouble : 0u);
        }

        const std::string xs = s.signXs(method, uri, a1, payload, tsMs);
        if (xs == expectXs) ++okXs;
        else if (firstBad.empty())
            firstBad = "xs#" + std::to_string(i);

        const std::string xsc = s.signXsCommon(a1, tsMs);
        if (xsc == expectXsc) ++okXsc;
        else if (firstBad.empty())
            firstBad = "xsc#" + std::to_string(i);
    }
    check(okXs == n, "x-s 差分 1000/1000",
          std::to_string(okXs) + "/" + std::to_string(n) + " " + firstBad);
    check(okXsc == n, "x-s-common 差分 1000/1000",
          std::to_string(okXsc) + "/" + std::to_string(n));

    // ── ③ 结构自证(重算组 0 输出)───────────────────────────
    {
        const cJSON* c0 = cJSON_GetArrayItem(const_cast<cJSON*>(cases), 0);
        rng.seq.clear();
        rng.i = 0;
        const cJSON* seqA = jarrOf(c0, "seq");
        const int sn = seqA ? cJSON_GetArraySize(const_cast<cJSON*>(seqA)) : 0;
        for (int k = 0; k < sn; ++k) {
            const cJSON* it = cJSON_GetArrayItem(const_cast<cJSON*>(seqA), k);
            rng.seq.push_back(cJSON_IsNumber(it) ? (uint32_t)it->valuedouble : 0u);
        }
        const std::string method = jstrOf(c0, "method");
        const std::string uri = jstrOf(c0, "uri");
        const std::string a1 = jstrOf(c0, "a1");
        std::string payload;
        {
            const cJSON* pit = cJSON_GetObjectItemCaseSensitive(c0, "payload");
            if (cJSON_IsString(pit)) payload = jstrOf(c0, "payload");
            else if (pit && !cJSON_IsNull(pit)) {
                char* js = cJSON_PrintUnformatted(const_cast<cJSON*>(pit));
                payload = js ? js : "";
                if (js) cJSON_free(js);
            }
        }
        const long long ts = jintOf(c0, "tsMs");

        const std::string xs = s.signXs(method, uri, a1, payload, ts);
        check(xs.compare(0, 4, "XYS_") == 0, "x-s XYS_ 前缀");

        const std::string inner = b64Decode(remapBack(
            xs.substr(4), s.config().customAlphabet));
        check(inner.find("\"x0\":\"4.3.5\"") != std::string::npos
                  && inner.find("\"x1\":\"xhs-pc-web\"") != std::string::npos
                  && inner.find("\"x2\":\"Windows\"") != std::string::npos
                  && inner.find("\"x4\":\"object\"") != std::string::npos,
              "x-s 内层模板字段");
        std::string x3;
        check(findStr(inner, "x3", &x3)
                  && x3.compare(0, 8, "mns0301_") == 0,
              "x3 前缀 mns0301_");
        if (x3.size() > 8) {
            const std::string x3raw = b64Decode(remapBack(
                x3.substr(8), s.config().x3Alphabet));
            check(x3raw.size() == 144, "x3 解出 144 字节",
                  std::to_string(x3raw.size()));
        }

        rng.i = 0;   // 重放须同一序列起点
        const std::string xs2 = s.signXs(method, uri, a1, payload, ts);
        check(xs2 == xs, "同种子重放确定(差分一致)");

        const std::string xsc = s.signXsCommon(a1, ts);
        const std::string scJson = b64Decode(remapBack(
            xsc, s.config().customAlphabet));
        check(scJson.find("\"s0\":5") != std::string::npos
                  && scJson.find("\"x11\":\"normal\"") != std::string::npos,
              "x-s-common 模板字段");
        std::string x5, x8;
        check(findStr(scJson, "x5", &x5) && x5 == a1, "x5 == a1");
        check(findStr(scJson, "x8", &x8) && !x8.empty(), "x8 存在");
        check(scJson.find("\"x4\":\"4.86.0\"") != std::string::npos, "x4 版本字段");

        const size_t p9 = scJson.find("\"x9\":");
        long long x9 = -1;
        if (p9 != std::string::npos)
            x9 = strtoll(scJson.c_str() + p9 + 5, 0, 10);
        const uint32_t u =
            (uint32_t)::crc32(0, (const Bytef*)x8.data(), (uInt)x8.size())
            ^ 0xEDB88320u;
        check(x9 == (long long)(int)u, "x9 == crc32(x8)^POLY(有符号)");

        // b1 密文长度合理(RC4 后 latin1→UTF-8 展开)
        const std::string b1Raw = b64Decode(remapBack(
            x8, s.config().customAlphabet));
        check(b1Raw.size() > 100, "b1 密文长度合理",
              std::to_string(b1Raw.size()));
    }

    // ── ④ cookie 工具(纯逻辑,离线)───────────────────────
    {
        using namespace qxv::xhssign;
        const std::string ck = "a1=K1; web_session=WS; xsecappid=xhs-pc-web";
        check(cookieA1(ck) == "K1", "cookieA1");
        check(hasWebSession(ck), "hasWebSession: 完整");
        check(!hasWebSession("a1=K1"), "hasWebSession: 缺 web_session");
        check(!hasWebSession("web_session=WS"), "hasWebSession: 缺 a1");
        check(cookieValue(ck, "xsecappid") == "xhs-pc-web", "cookieValue 任意键");
    }

    cJSON_Delete(root);
    std::printf("xhssign: %s\n", g_fail ? "FAILED" : "all green");
    return g_fail ? 1 : 0;
}
