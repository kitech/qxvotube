#pragma once
#include <QByteArray>
#include <QString>

namespace qxv {
namespace hgcenc {

// 红果 spade_a -> content key 纯离线 unwrap(ver1 路径,播放器 opt 0x97 flag=0)。
// 逆向口径: libttmplayer.so FUN_001c4550(zhangbaio/hongguo frida/unwrap_spade.py,
// 5 组运行时真值 100% 对拍)。算法 = 纯字节变换(XOR + POPCOUNT + 位置相关),
// 无 KEK、无 AES。
// 链路: video_model.encryptInfo.spadeA(base64)
//       -> base64 解码(调用方完成) -> unwrapV1 -> content key(16B, 32 hex)
//       -> 配合密文 senc 首样本 base_iv -> AES-128-CTR 逐样本解密(见 cenc.h)。
// 注: ver2(spade type "app_v2"/"web_v2")走 AES-GCM-256 路径,红果视频未见使用,
//     本实现检测到 v2 返回空串(不静默猜)。
class Spade {
public:
    // spade: 37 字节原始数据(base64 解码后);flag: 播放器 opt 0x97 值(实测 0)。
    // 返回 32 字符小写 hex 的 content key;格式异常/type=v2/长度不够返回空串。
    static QString unwrapV1(const QByteArray& spade, int flag = 0);
};

} // namespace hgcenc
} // namespace qxv
