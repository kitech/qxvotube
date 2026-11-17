#pragma once
#include <stdint.h>
#include "compat.h"

// 红果 CENC 密文 mp4 的 box 解析(手写,禁字符串搜索——流式对齐需偏移精确)。
// 口径对齐 zhangbaio/hongguo frida/decutil.py + oracle.py:
//   每轨 base_iv = 本轨 senc 盒首样本 IV 的前 8 字节(视频/音频各取各的);
//   tenc.default_IV_size / default_KID 自 stsd entry 的 sinf/schi 取。
// 渐进式 mp4(App 通道): senc 在 moov 内 stbl 下,样本序号全文件统一;
// fMP4(DASH): senc 在各 moof/traf 下(H2b 本地代理时按 moof 解析)。
namespace qxv {
namespace hgcenc {

struct TrackInfo {
    bool isVideo = false;       // hdlr == 'vide'
    uint32_t ivSize = 0;         // tenc.default_IV_size(8/16)
    QByteArray kid;             // tenc.default_KID(16B)
    uint64_t baseIv = 0;         // 本轨 senc 首样本 IV 前 8 字节(BE)
    QList36<uint32_t> sampleSizes; // stsz
    QList36<uint64_t> sampleOffsets; // stco/co64 + stsc 展开后的样本绝对偏移
};

// 遍历 [off,end) 内的一层 box;cb(type 四字符, box 内容区起始偏移, 内容长度)。
// 返回 false 中止。size==0(到末尾)/size==1(64 位扩展)按 ISO 14496-12 处理。
typedef bool (*BoxCb)(uint32_t type, uint64_t contentOff, uint64_t contentSize,
                      void* ud);
bool walkBoxes(const QByteArray& buf, uint64_t off, uint64_t end, BoxCb cb,
               void* ud);

// 解析全部加密轨(视频/音频)。只处理带 senc 的轨;未加密轨跳过。
// 返回 0 成功;-1 结构不符(moov/trak/stbl 缺失等)。
int findTracks(const QByteArray& buf, QList36<TrackInfo>& out);

} // namespace hgcenc
} // namespace qxv
