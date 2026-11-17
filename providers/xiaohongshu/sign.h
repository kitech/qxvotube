#pragma once
// 小红书 web 登录态签名器(xhshow 移植, 2026-11-17)
//
// 产出两个 HTTP 头:
//   x-s        = "XYS_" + 主表B64( {"x0","x1","x2","x3":"mns0301_"+X3B64(payload),"x4"} )
//   x-s-common = 主表B64( {"s0","s1","x0".."x11","x5":a1,"x8":b1,"x9":crc32(b1)^POLY} )
//
// 设计:**零 Qt 依赖**(std::string + <stdint.h> + std::mutex)。
//   理由:Qt 3.3 无 quint32/qPrintable,QByteArray 为 QMemArray<char> 残缺 API,
//   纯 C++ 实现可在 Qt3/Qt4/Qt5/Qt6 任意宿主直接编译,Qt3 支持是"能编能跑",
//   不是"写了个 Qt3 分支但从没编过"(见 tests/qt3sign,用 Qt 3.3.8sh 实测)。
//
// 依赖:
//   MD5   = qlcomp md5.{h,c}(纯 C)
//   RC4   = vendor/wjcryptlib(WjCryptLib_Rc4,公有领域)
//   CRC32 = 系统 zlib(默认) / vendor/zlib-crc32(QXV_NO_ZLIB 兜底)
//   B64   = 本文件内自带标准编码(字母表可替换,见 customB64/x3B64)
//
// 口径:
//   - JSON 键序固定,手工拼接(不用 cJSON,保序且零依赖)
//   - b1 的 URL-quote 展开等价于 latin1→UTF-8 展开(差分 fixture 证实)
//   - x9 = zlib_crc32(b1) ^ 0xEDB88320,按有符号 32 位解读
//   - RNG/时间可注入(SeqRng),输出确定 —— 差分对拍前提
//
// 验证:tests/xhssign.test.cpp(KAT + 1000 组差分对拍 + 结构自证,全离线);
//       tests/qt3sign 用 Qt 3.3.8sh 构建同一份源码再跑一遍。

#include <stdint.h>

#include <string>

namespace qxv { namespace xhssign {

// ── cookie 工具(纯 C++,可离线单测)───────────────────────────
// 从 "a=1; b=2" 形式 cookie 串取指定键值;缺失返回空串。
std::string cookieValue(const std::string& cookie, const std::string& name);
// 登录态判定:必须同时有 web_session 与 a1(a1 用于 b1 指纹签名)
bool hasWebSession(const std::string& cookie);
// 取 cookie 里的 a1;缺失返回空
std::string cookieA1(const std::string& cookie);

// 随机源注桩:测试注入固定序列 → 输出确定
// 消费顺序(与 xhshow 随机调用序一一对应):
//   signXs:      [seed32, offset(10..50), seq(15..50), wpl(1000..1200)]
//   signXsCommon: [x36(1..20)]
struct RandomSource {
    virtual ~RandomSource() {}
    virtual uint32_t seed32() = 0;
    virtual int intInRange(int lo, int hi) = 0;
};

// 生产实现:xorshift32(时间+地址播撒;线程安全)
RandomSource* defaultRandomSource();

struct SignConfig {
    std::string customAlphabet;   // x-s/x-s-common 的 base64 替换表(64 字符)
    std::string x3Alphabet;       // x3 payload 段的 base64 替换表
    std::string hexKey;           // payload 异或键(288 hex = 144 字节)
    std::string b1Key;            // RC4 b1 指纹密钥
    SignConfig();
};

class Signer {
public:
    // rng 非拥有(调用方保活);nullptr = 内部共享实例
    explicit Signer(const SignConfig& cfg = SignConfig(),
                    RandomSource* rng = nullptr);
    ~Signer();

    // x-s。method:"GET"/"POST"
    //   POST:payload 为紧凑 JSON(键序平台一致,调用方拼好)
    //   GET :payload 为已编码 query 串(不含 '?',用 api.h queryEncode 生成)
    //        content = uri + "?" + payload(xhshow _build_content_string)
    // a1 = cookie a1 值;timestampMs < 0 取当前时间。
    std::string signXs(const std::string& method,
                       const std::string& uri,          // 不含 query
                       const std::string& a1,
                       const std::string& payload = std::string(),
                       int64_t timestampMs = -1);

    // x-b3-traceid(16 位小写 hex,随机源可注入)
    std::string b3TraceId();

    // x-s-common。x36 段消耗一次 intInRange(1,20);timestampMs 仅入 x44。
    std::string signXsCommon(const std::string& a1, int64_t timestampMs = -1);

    // 供测试/对拍:原始 payload(未异或,144 字节)
    std::string buildPayload(const std::string& dHex, const std::string& mHex,
                             const std::string& a1, const std::string& appId,
                             const std::string& contentString, int64_t tsMs);

    // 供测试:144 字节异或后结果
    std::string xorKeyed(const std::string& payload) const;

    // 供测试:只读配置(字母表用于反解自证)
    const SignConfig& config() const { return m_cfg; }

private:
    std::string customB64(const std::string& raw) const;  // 标准B64→主表替换
    std::string x3B64(const std::string& raw) const;      // 标准B64→x3表替换
    std::string rc4(const std::string& key, const std::string& data) const;
    std::string customHashV2(const std::string& in) const;  // 8k 字节→16 字节
    uint32_t crc32Bytes(const std::string& b) const;      // zlib 口径(首传 0)
    std::string md5Hex(const std::string& data) const;

    SignConfig m_cfg;
    RandomSource* m_rng;
};

}} // namespace qxv::xhssign
