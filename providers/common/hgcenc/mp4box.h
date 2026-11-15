#pragma once
#include <QByteArray>
#include <QList>
#include <QtGlobal>

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
    quint32 ivSize = 0;         // tenc.default_IV_size(8/16)
    QByteArray kid;             // tenc.default_KID(16B)
    quint64 baseIv = 0;         // 本轨 senc 首样本 IV 前 8 字节(BE)
    QList<quint32> sampleSizes; // stsz
    QList<quint64> sampleOffsets; // stco/co64 + stsc 展开后的样本绝对偏移
};

// 遍历 [off,end) 内的一层 box;cb(type 四字符, box 内容区起始偏移, 内容长度)。
// 返回 false 中止。size==0(到末尾)/size==1(64 位扩展)按 ISO 14496-12 处理。
typedef bool (*BoxCb)(quint32 type, quint64 contentOff, quint64 contentSize,
                      void* ud);
bool walkBoxes(const QByteArray& buf, quint64 off, quint64 end, BoxCb cb,
               void* ud);

// 解析全部加密轨(视频/音频)。只处理带 senc 的轨;未加密轨跳过。
// 返回 0 成功;-1 结构不符(moov/trak/stbl 缺失等)。
int findTracks(const QByteArray& buf, QList<TrackInfo>& out);

} // namespace hgcenc
} // namespace qxv
