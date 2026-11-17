#pragma once
#include <stdint.h>
#include "compat.h"
#include "mp4box.h"

namespace qxv {
namespace hgcenc {

// 红果 CENC-CTR 全轨解密(纯离线,口径 zhangbaio/hongguo frida/decutil.py):
//   每轨 base_iv = 本轨 senc 首样本 IV 前 8 字节;
//   样本 idx 的计数器初值(128bit BE) = ((baseIv + idx) << 64) 即
//   高 8 字节 = baseIv+idx,低 8 字节 = 0;
//   AES-128-CTR(key16) 对每样本独立起表解密。
// 视频轨解密后 NAL 自证: 4 字节长度前缀的 NAL 长度和恰等于样本 size。
class Cenc {
public:
    // 就地解密 buf 中 track 的全部样本。返回成功解密样本数;解析/结构错返回负数。
    // key: 16 字节(unwrapV1 的 32hex 由调用方 fromHex)。
    static int decryptTrack(QByteArray& buf, const TrackInfo& track,
                            const QByteArray& key);

    // NAL 自证(视频样本): length-prefixed 长度和 == sz 即真(zhangbaio _nal_ok)。
    static bool nalOk(const char* p, int sz);
};

} // namespace hgcenc
} // namespace qxv
