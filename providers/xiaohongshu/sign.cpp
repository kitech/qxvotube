#include "sign.h"

#include "../../qlcomp/md5.h"
extern "C" {   // WjCryptLib 头无 extern "C" 包裹,自补
#include "../../vendor/wjcryptlib/WjCryptLib_Rc4.h"
}
#if defined(QXV_NO_ZLIB)
#include "../../vendor/zlib-crc32/crc32.h"   // qxv_crc32 兜底
#else
#include <zlib.h>                            // crc32()
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mutex>
#include <string>

namespace qxv { namespace xhssign {

namespace {

// ── 常量(xhshow config.py / fingerprint_data.py 逐项对齐,2026-11-17 版)──
const uint8_t kVersionBytes[4] = {121, 104, 96, 41};          // VERSION_BYTES
const uint8_t kA3Prefix[4]     = {2, 97, 51, 16};             // A3_PREFIX
const uint8_t kEnvTable[15] =
    {115, 248, 83, 102, 103, 201, 181, 131, 99, 94, 4, 68, 250, 132, 21};
const uint8_t kEnvChecks[15] =
    {0, 1, 18, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0};
// custom_hash_v2 初始向量 = xxHash 四质数
const uint32_t kHashIv[4] = {1831565813u, 461845907u, 2246822507u, 3266489909u};
const uint32_t kCrcPoly = 0xEDB88320u;

const int kPayloadLen = 144;   // payload 长度(与 xhshow PAYLOAD_LENGTH 一致)
const int kA1Len      = 52;
const int kAppIdLen   = 10;
const int kMd5XorLen  = 8;
const int kTsLeLen    = 8;
const char kXysPrefix[] = "XYS_";
const char kX3Prefix[]  = "mns0301_";
const char kAppId[]     = "xhs-pc-web";
// b1 指纹 canvas hash(fingerprint_data.CANVAS_HASH,2026-11-17 版本)
const char kCanvasHash[] = "742cc32c";
const char kStdB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ── 小工具(纯 C++)─────────────────────────────────────────
std::string latin1ToUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 3 / 2 + 2);
    for (size_t i = 0; i < in.size(); ++i) {
        const uint8_t b = (uint8_t)in[i];
        if (b < 0x80) {
            out.push_back((char)b);
        } else {
            out.push_back((char)(0xC0 | (b >> 6)));
            out.push_back((char)(0x80 | (b & 0x3f)));
        }
    }
    return out;
}

std::string hexToBytes(const std::string& hex) {
    std::string out;
    const size_t n = hex.size() / 2;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const char hi = hex[2 * i];
        const char lo = hex[2 * i + 1];
        const int a = (hi >= '0' && hi <= '9') ? hi - '0'
                    : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                    : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : 0;
        const int b = (lo >= '0' && lo <= '9') ? lo - '0'
                    : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                    : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : 0;
        out[i] = (char)((a << 4) | b);
    }
    return out;
}

void padTo(std::string& a, size_t len, char pad) {
    if (a.size() >= len) return;
    a.resize(len, pad);   // std::string::resize 补 pad(Qt3 无 leftJustified)
}

void le(std::string& a, uint64_t v, int n) {
    for (int i = 0; i < n; ++i) a.push_back((char)((v >> (8 * i)) & 0xff));
}

// 标准 base64 编码(自实现,零 Qt;'=' 补齐)
std::string b64Encode(const std::string& raw) {
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < raw.size()) {
        const uint32_t trip = ((uint32_t)(uint8_t)raw[i] << 16)
                            | ((uint32_t)(uint8_t)raw[i + 1] << 8)
                            | (uint32_t)(uint8_t)raw[i + 2];
        out.push_back(kStdB64[(trip >> 18) & 0x3f]);
        out.push_back(kStdB64[(trip >> 12) & 0x3f]);
        out.push_back(kStdB64[(trip >> 6) & 0x3f]);
        out.push_back(kStdB64[trip & 0x3f]);
        i += 3;
    }
    if (i + 1 == raw.size()) {
        const uint32_t trip = ((uint32_t)(uint8_t)raw[i] << 16);
        out.push_back(kStdB64[(trip >> 18) & 0x3f]);
        out.push_back(kStdB64[(trip >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == raw.size()) {
        const uint32_t trip = ((uint32_t)(uint8_t)raw[i] << 16)
                            | ((uint32_t)(uint8_t)raw[i + 1] << 8);
        out.push_back(kStdB64[(trip >> 18) & 0x3f]);
        out.push_back(kStdB64[(trip >> 12) & 0x3f]);
        out.push_back(kStdB64[(trip >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

// 标准 B64 → 目标替换表;'=' 不动
std::string b64Remap(const std::string& raw, const std::string& dstTab) {
    const std::string std64 = b64Encode(raw);
    std::string out;
    out.reserve(std64.size());
    for (size_t i = 0; i < std64.size(); ++i) {
        const char c = std64[i];
        if (c == '=') { out.push_back('='); continue; }
        const size_t idx = strchr(kStdB64, c) ? (size_t)(strchr(kStdB64, c) - kStdB64) : 0;
        if (strchr(kStdB64, c) && idx < dstTab.size())
            out.push_back(dstTab[idx]);
        else
            out.push_back(c);   // 未知字符原样(防御)
    }
    return out;
}

// xorshift32 共享实例(线程安全)
struct DefaultRng : RandomSource {
    uint32_t s;
    DefaultRng() {
        const uint32_t t = (uint32_t)time(0);
        s = t ^ (uint32_t)(size_t)this;
        s |= 1u;
    }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    uint32_t seed32() {
        static std::mutex m;
        std::lock_guard<std::mutex> lk(m);
        return next();
    }
    int intInRange(int lo, int hi) {
        return lo + (int)(seed32() % (uint32_t)(hi - lo + 1));
    }
};

DefaultRng& sharedRng() {
    static DefaultRng r;
    return r;
}

} // namespace

SignConfig::SignConfig()
    : customAlphabet(
          "ZmserbBoHQtNP+wOcza/LpngG8yJq42KWYj0DSfdikx3VT16IlUAFM97hECvuRX5")
    , x3Alphabet(
          "MfgqrsbcyzPQRStuvC7mn501HIJBo2DEFTKdeNOwxWXYZap89+/A4UVLhijkl63G")
    , hexKey(
          "71a302257793271ddd273bcee3e4b98d9d7935e1da33f5765e2ea8afb6dc77a51"
          "a499d23b67c20660025860cbf13d4540d92497f58686c574e508f46e1956344f3"
          "9139bf4faf22a3eef120b79258145b2feb5193b6478669961298e79bedca646e1"
          "a693a926154a5a7a1bd1cf0dedb742f917a747a1e388b234f2277516db7116035"
          "439730fa61e9822a0eca7bff72d8")
    , b1Key("xhswebmplfbt")
{}

RandomSource* defaultRandomSource() { return &sharedRng(); }

std::string cookieValue(const std::string& cookie, const std::string& name) {
    size_t pos = 0;
    while (pos < cookie.size()) {
        size_t end = cookie.find(';', pos);
        if (end == std::string::npos) end = cookie.size();
        const size_t eq = cookie.find('=', pos);
        if (eq != std::string::npos && eq < end) {
            std::string k = cookie.substr(pos, eq - pos);
            while (!k.empty() && (k[0] == ' ' || k[0] == '\t')) k.erase(0, 1);
            while (!k.empty() && (k[k.size() - 1] == ' ' || k[k.size() - 1] == '\t'))
                k.erase(k.size() - 1);
            if (k == name) {
                std::string v = cookie.substr(eq + 1, end - eq - 1);
                while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
                while (!v.empty() &&
                       (v[v.size() - 1] == ' ' || v[v.size() - 1] == '\t'))
                    v.erase(v.size() - 1);
                return v;
            }
        }
        pos = end + 1;
    }
    return std::string();
}

std::string cookieA1(const std::string& cookie) {
    return cookieValue(cookie, "a1");
}

bool hasWebSession(const std::string& cookie) {
    return !cookieValue(cookie, "web_session").empty()
        && !cookieA1(cookie).empty();
}

Signer::Signer(const SignConfig& cfg, RandomSource* rng)
    : m_cfg(cfg), m_rng(rng ? rng : &sharedRng()) {}
Signer::~Signer() {}

// ── 原语 ─────────────────────────────────────────────────────────
std::string Signer::md5Hex(const std::string& data) const {
    MD5_CTX c;
    MD5_Init(&c);
    MD5_Update(&c, (const uint8_t*)data.data(), (size_t)data.size());
    uint8_t d[16];
    MD5_Final(d, &c);
    static const char* h = "0123456789abcdef";
    std::string hex;
    hex.resize(32);
    for (int i = 0; i < 16; ++i) {
        hex[2 * i]     = h[d[i] >> 4];
        hex[2 * i + 1] = h[d[i] & 15];
    }
    return hex;
}

std::string Signer::rc4(const std::string& key, const std::string& data) const {
    // WjCryptLib 口径:Rc4XorWithKey(key, keySize, DropN, in, out, len)
    std::string out;
    out.resize(data.size());
    if (!data.empty())
        Rc4XorWithKey((const uint8_t*)key.data(), (uint32_t)key.size(), 0,
                      data.data(), &out[0], (uint32_t)data.size());
    return out;
}

uint32_t Signer::crc32Bytes(const std::string& b) const {
    if (b.empty()) return 0;
#if defined(QXV_NO_ZLIB)
    return qxv_crc32(0, (const uint8_t*)b.data(), b.size());
#else
    return (uint32_t)::crc32(0, (const Bytef*)b.data(), (uInt)b.size());
#endif
}

std::string Signer::customB64(const std::string& r) const {
    return b64Remap(r, m_cfg.customAlphabet);
}

std::string Signer::x3B64(const std::string& r) const {
    return b64Remap(r, m_cfg.x3Alphabet);
}

// custom_hash_v2(xhshow crypto.py _custom_hash_v2 逐行对齐;8k 入→16 出)
std::string Signer::customHashV2(const std::string& in) const {
    const int len = (int)in.size();
    const uint8_t* p = (const uint8_t*)in.data();
    uint32_t s0 = kHashIv[0] ^ (uint32_t)len;
    uint32_t s1 = kHashIv[1] ^ ((uint32_t)len << 8);
    uint32_t s2 = kHashIv[2] ^ ((uint32_t)len << 16);
    uint32_t s3 = kHashIv[3] ^ ((uint32_t)len << 24);
#define XHS_ROTL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
    for (int i = 0; i < len / 8; ++i) {
        const uint32_t v0 = (uint32_t)p[8*i] | ((uint32_t)p[8*i+1] << 8)
                          | ((uint32_t)p[8*i+2] << 16) | ((uint32_t)p[8*i+3] << 24);
        const uint32_t v1 = (uint32_t)p[8*i+4] | ((uint32_t)p[8*i+5] << 8)
                          | ((uint32_t)p[8*i+6] << 16) | ((uint32_t)p[8*i+7] << 24);
        s0 = XHS_ROTL((s0 + v0) ^ s2, 7);
        s1 = XHS_ROTL((v0 ^ s1) + s3, 11);
        s2 = XHS_ROTL((s2 + v1) ^ s0, 13);
        s3 = XHS_ROTL((s3 ^ v1) + s1, 17);
    }
    const uint32_t t0 = s0 ^ (uint32_t)len;
    const uint32_t t1 = s1 ^ t0;
    const uint32_t t2 = s2 + t1;
    const uint32_t t3 = s3 ^ t2;
    const uint32_t r0 = XHS_ROTL(t0, 9);
    const uint32_t r1 = XHS_ROTL(t1, 13);
    const uint32_t r2 = XHS_ROTL(t2, 17);
    const uint32_t r3 = XHS_ROTL(t3, 19);
    const uint32_t o0 = r0 + r2;
    const uint32_t o1 = r1 ^ r3;
    const uint32_t o2 = r2 + o0;
    const uint32_t o3 = r3 ^ o1;
#undef XHS_ROTL
    std::string out;
    out.resize(16);
    const uint32_t w[4] = {o0, o1, o2, o3};
    for (int i = 0; i < 4; ++i)                 // little-endian 输出
        for (int k = 0; k < 4; ++k)
            out[i * 4 + k] = (char)((w[i] >> (8 * k)) & 0xff);
    return out;
}

// ── payload(144 字节;build_payload_array 逐段对齐)───────────────
std::string Signer::buildPayload(const std::string& dHex, const std::string& mHex,
                                 const std::string& a1, const std::string& appId,
                                 const std::string& contentString, int64_t tsMs) {
    const uint32_t seed = m_rng->seed32();
    const uint8_t seedByte = (uint8_t)(seed & 0xff);
    std::string pl;
    pl.reserve(kPayloadLen);
    pl.append((const char*)kVersionBytes, 4);
    le(pl, seed, 4);
    le(pl, (uint64_t)tsMs, kTsLeLen);                    // ts_bytes(int(ts*1000))
    // 无 session 分支(xhshow 默认路径):
    const int off = m_rng->intInRange(10, 50);
    le(pl, (uint64_t)(tsMs - (int64_t)off * 1000), kTsLeLen);   // effective_ts_ms
    const int seq = m_rng->intInRange(15, 50);
    le(pl, (uint64_t)seq, 4);
    const int wpl = m_rng->intInRange(1000, 1200);
    le(pl, (uint64_t)wpl, 4);
    le(pl, (uint64_t)contentString.size(), 4);           // uri_length(utf8)
    // md5(d) 前 8 字节 ^ seedByte
    const std::string dRaw = hexToBytes(dHex);
    for (int i = 0; i < kMd5XorLen; ++i)
        pl.push_back((char)((uint8_t)dRaw[i] ^ seedByte));
    // a1(52 定长,\0 补齐)
    std::string a1b = a1;
    if (a1b.size() > (size_t)kA1Len) a1b = a1b.substr(0, kA1Len);
    padTo(a1b, kA1Len, '\0');
    pl.push_back((char)kA1Len);
    pl.append(a1b);
    // appid(10 定长)
    std::string apb = appId;
    if (apb.size() > (size_t)kAppIdLen) apb = apb.substr(0, kAppIdLen);
    padTo(apb, kAppIdLen, '\0');
    pl.push_back((char)kAppIdLen);
    pl.append(apb);
    // part11: [1, seedByte^ENV[0]] + ENV[i]^CHECKS[i](i=1..14)
    pl.push_back((char)1);
    pl.push_back((char)(seedByte ^ kEnvTable[0]));
    for (int i = 1; i < 15; ++i)
        pl.push_back((char)(kEnvTable[i] ^ kEnvChecks[i]));
    // A3_PREFIX + (hash(ts_bytes + md5_path16) 逐字节 ^ seedByte);
    //   md5_path 只取前 16 字节(range(0,32,2)),不整段进 payload。
    pl.append((const char*)kA3Prefix, 4);
    const std::string tsBytes = pl.substr(4 + 4, kTsLeLen);
    const std::string md5Path16 = hexToBytes(mHex).substr(0, 16);
    const std::string h = customHashV2(tsBytes + md5Path16);
    for (size_t i = 0; i < h.size(); ++i)
        pl.push_back((char)((uint8_t)h[i] ^ seedByte));
    return pl;   // 4+4+8+8+4+4+4+8+53+11+16+4+16 = 144
}

std::string Signer::xorKeyed(const std::string& payload) const {
    const std::string key = hexToBytes(m_cfg.hexKey);
    std::string out = payload;
    for (size_t i = 0; i < out.size() && i < key.size(); ++i)
        out[i] = (char)(out[i] ^ key[i]);
    return out;
}

// ── 对外:signXs ──────────────────────────────────────────────────
std::string Signer::signXs(const std::string& method, const std::string& uri,
                           const std::string& a1, const std::string& payload,
                           int64_t timestampMs) {
    if (timestampMs < 0) timestampMs = (int64_t)time(0) * 1000;
    // content_string(_build_content_string):
    //   POST = uri + payload;GET = uri [? + query](空则不拼)
    const bool isPost = method == "POST";
    const std::string content =
        isPost ? uri + payload
               : (payload.empty() ? uri : uri + "?" + payload);
    const std::string dHex = md5Hex(content);
    const std::string mHex = isPost ? md5Hex(uri) : dHex;

    const std::string pl = buildPayload(dHex, mHex, a1, kAppId, content,
                                        timestampMs);
    const std::string x3 = x3B64(xorKeyed(pl).substr(0, kPayloadLen));

    // SIGNATURE_DATA_TEMPLATE 手工拼接(键序与 Python json.dumps 一致)
    std::string json = "{\"x0\":\"4.3.5\",\"x1\":\"xhs-pc-web\","
                       "\"x2\":\"Windows\",\"x3\":\"";
    json += kX3Prefix;
    json += x3;
    json += "\",\"x4\":\"object\"}";
    return std::string(kXysPrefix) + customB64(json);
}

// ── 对外:signXsCommon ────────────────────────────────────────────
std::string Signer::b3TraceId() {
    static const char* kHex = "abcdef0123456789";   // xhshow HEX_CHARS
    std::string out;
    out.reserve(16);
    uint32_t v = m_rng->seed32();
    for (int i = 0; i < 16; ++i) {
        if (i == 8) v = m_rng->seed32();
        out.push_back(kHex[(v >> ((i % 8) * 4)) & 0x0f]);
    }
    return out;
}

std::string Signer::signXsCommon(const std::string& a1, int64_t timestampMs) {
    if (timestampMs < 0) timestampMs = (int64_t)time(0) * 1000;
    // b1 指纹(仅 generate_b1 的 18 字段子集;动态仅 x36/x44,其余常量)。
    // b1_json 键序与 Python b1_fp 插入序一致。
    const int x36 = m_rng->intInRange(1, 20);
    char x36s[16];
    snprintf(x36s, sizeof(x36s), "%d", x36);
    char tsBuf[32];
    snprintf(tsBuf, sizeof(tsBuf), "%lld", (long long)timestampMs);
    std::string b1Json =
        "{\"x33\":\"0\",\"x34\":\"0\",\"x35\":\"0\",\"x36\":\"";
    b1Json += x36s;
    b1Json += "\",\"x37\":\"0|0|0|0|0|0|0|0|0|1|0|0|0|0|0|0|0|0|1|0|0|0|0|0\","
              "\"x38\":\"0|0|1|0|1|0|0|0|0|0|1|0|1|0|1|0|0|0|0|0|0|0|0|0|0|0|"
              "0|0|0|0|0|0|0|0|0|0|0|0|0\","
              "\"x39\":0,\"x42\":\"3.4.4\",\"x43\":\"";
    b1Json += kCanvasHash;
    b1Json += "\",\"x44\":\"";
    b1Json += tsBuf;
    b1Json += "\",\"x45\":\"__SEC_CAV__1-1-1-1-1|__SEC_WSA__|\","
              "\"x46\":\"false\",\"x48\":\"\",\"x49\":\"{list:[],type:}\","
              "\"x50\":\"\",\"x51\":\"\",\"x52\":\"\","
              "\"x82\":\"_0x17a2|_0x1954\"}";
    const std::string b1Cipher = rc4(m_cfg.b1Key, b1Json);
    // xhshow 的 URL-quote 展开:quote(latin1 密文) 默认按 UTF-8 重编码,
    // 再逐 %XX 还原字节 ⇒ 等价于 latin1→UTF-8 展开(0x80+ 变两字节)。
    const std::string b1Raw = latin1ToUtf8(b1Cipher);
    const std::string b1 = customB64(b1Raw);

    // x9 = (-1 ^ c ^ POLY) 有符号;c 为中间态 = ~zlib_out
    //   ⇒ x9 = zlib_out ^ POLY(有符号 32 位解读)
    const uint32_t u = crc32Bytes(b1) ^ kCrcPoly;
    const int x9 = (int)u;
    char x9s[24];
    snprintf(x9s, sizeof(x9s), "%d", x9);

    // SIGNATURE_XSCOMMON_TEMPLATE(x5/x8/x9 替换;键序同 Python)
    std::string json = "{\"s0\":5,\"s1\":\"\",\"x0\":\"1\",\"x1\":\"4.3.5\","
                       "\"x2\":\"Windows\",\"x3\":\"xhs-pc-web\","
                       "\"x4\":\"4.86.0\",\"x5\":\"";
    json += a1;
    json += "\",\"x6\":\"\",\"x7\":\"\",\"x8\":\"";
    json += b1;
    json += "\",\"x9\":";
    json += x9s;
    json += ",\"x10\":0,\"x11\":\"normal\"}";
    return customB64(json);
}

}} // namespace qxv::xhssign
