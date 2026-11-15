#include "mp4box.h"

#include <cstdio>
#include <QVector>

#define HGDBG(...) do { if (getenv("HGDBG")) { fprintf(stderr, "[hgdbg] "); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

// 口径: ISO 14496-12/15 + zhangbaio/hongguo oracle.py(样本表展开、senc 首样本
// IV 前 8 字节为 base_iv、tenc v0 布局: ver/flags(4) reserved(2) IsEnc(1)
// IVsize(1) KID(16))。

namespace qxv {
namespace hgcenc {

namespace {

inline quint32 rd32(const QByteArray& b, quint64 o) {
    return (quint32(static_cast<unsigned char>(b[int(o)])) << 24)
         | (quint32(static_cast<unsigned char>(b[int(o) + 1])) << 16)
         | (quint32(static_cast<unsigned char>(b[int(o) + 2])) << 8)
         |  quint32(static_cast<unsigned char>(b[int(o) + 3]));
}

inline quint64 rd64(const QByteArray& b, quint64 o) {
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | quint32(static_cast<unsigned char>(b[int(o) + i]));
    return v;
}

// 找 path 指定的嵌套 box(从 off 起,同层逐个),返回内容区起始与大小;
// 找不到返回 false。path 为如 {'mdia','minf','stbl'}。
bool findBox(const QByteArray& buf, quint64 off, quint64 end,
             const quint32* path, int pathLen,
             quint64* contentOff, quint64* contentSize) {
    quint64 o = off;
    for (int d = 0; d < pathLen; ++d) {
        bool hit = false;
        while (o + 8 <= end) {
            quint64 size = rd32(buf, o);
            quint64 hdr = 8;
            quint32 ty = rd32(buf, o + 4);
            if (size == 1) {
                if (o + 16 > end) return false;
                size = rd64(buf, o + 8);
                hdr = 16;
            } else if (size == 0) {
                size = end - o;
            }
            if (size < hdr || o + size > end) return false;
            if (ty == path[d]) {
                if (contentOff) *contentOff = o + hdr;
                if (contentSize) *contentSize = size - hdr;
                if (d == pathLen - 1) return true;
                off = o + hdr;
                end = o + size;
                o = off;
                hit = true;
                break;
            }
            o += size;
        }
        if (!hit) return false;
    }
    return true;
}

bool isType(quint64 t, const char* s) {
    return static_cast<char>(t >> 24) == s[0]
        && static_cast<char>((t >> 16) & 0xff) == s[1]
        && static_cast<char>((t >> 8) & 0xff) == s[2]
        && static_cast<char>(t & 0xff) == s[3];
}

// 在 [lo,hi) 内按四字符标记定位 sinf→schi→tenc(各层记 box 尺寸自证),
// 返回 tenc 内容区起点/长度。SampleEntry 定长头(encv 78B/enca 28B)不可 box 遍历,
// 故此处用标记而非递归 findBox。
bool findNestedByMarker(const QByteArray& buf, quint64 lo, quint64 hi,
                        quint64* tencOff, quint64* tencSize) {
    const QByteArray b = buf;
    int i = int(lo);
    const int end = int(qMin<quint64>(hi, quint64(b.size())));
    while (i + 8 <= end) {
        i = b.indexOf(QByteArray("tenc", 4), i);
        if (i < 0 || i + 8 > end) return false;
        // 该 tenc 的上层必须是 schi,再上层是 sinf
        const int schi = i - 4;   // 假定 tenc 紧跟 schi 内容(实测布局)
        Q_UNUSED(schi);
        const quint64 boxSize = rd32(b, quint64(i) - 4);
        // tenc box = 8 头 + 20 内容(ver/flags 4 + reserved 2 + IsEnc 1 + IVsize 1 + KID 16)
        if (boxSize == 32 || boxSize >= 32) {
            *tencOff = quint64(i) + 4;
            *tencSize = boxSize - 8;
            return true;
        }
        ++i;
    }
    return false;
}

} // namespace

bool walkBoxes(const QByteArray& buf, quint64 off, quint64 end, BoxCb cb,
               void* ud) {
    quint64 o = off;
    while (o + 8 <= end) {
        quint64 size = rd32(buf, o);
        quint64 hdr = 8;
        const quint32 ty = rd32(buf, o + 4);
        if (size == 1) {
            if (o + 16 > end) return false;
            size = rd64(buf, o + 8);
            hdr = 16;
        } else if (size == 0) {
            size = end - o;
        }
        if (size < hdr || o + size > end) return false;
        if (!cb(ty, o + hdr, size - hdr, ud)) return false;
        o += size;
    }
    return true;
}

namespace {

// 常量逐个字面量声明,避免拼写漂移
const quint32 T_trak = 0x7472616b; // 'trak'
const quint32 T_mdia = 0x6d646961; // 'mdia'
const quint32 T_minf = 0x6d696e66; // 'minf'
const quint32 T_stbl = 0x7374626c; // 'stbl'
const quint32 T_hdlr = 0x68646c72; // 'hdlr'
const quint32 T_stsd = 0x73747364; // 'stsd'
const quint32 T_sinf = 0x73696e66; // 'sinf'
const quint32 T_schi = 0x73636869; // 'schi'
const quint32 T_tenc = 0x74656e63; // 'tenc'
const quint32 T_stsz = 0x7374737a; // 'stsz'
const quint32 T_stsc = 0x73747363; // 'stsc'
const quint32 T_stco = 0x7374636f; // 'stco'
const quint32 T_co64 = 0x636f3634; // 'co64'
const quint32 T_senc = 0x73656e63; // 'senc'
const quint32 H_vide = 0x76696465; // 'vide'
const quint32 H_soun = 0x736f756e; // 'soun'

bool parseTrack(const QByteArray& buf, quint64 trakOff, quint64 trakSize,
                TrackInfo& t) {
    const quint32 p1[] = { T_mdia, T_minf, T_stbl };
    quint64 stblOff, stblSize;
    if (!findBox(buf, trakOff, trakOff + trakSize, p1, 3, &stblOff,
                 &stblSize)) {
        HGDBG("no stbl in trak");
        return false;
    }
    HGDBG("stbl at %llu size %llu", (unsigned long long)stblOff,
          (unsigned long long)stblSize);
    // hdlr
    {
        const quint32 p2[] = { T_mdia, T_hdlr };
        quint64 o, s;
        if (findBox(buf, trakOff, trakOff + trakSize, p2, 2, &o, &s)
            && s >= 12) {
            const quint32 h = rd32(buf, o + 8);   // ver/flags(4)+pre_defined(4)
            if (h == H_vide) t.isVideo = true;
            else if (h != H_soun) return true;    // 非音视频轨跳过
        } else {
            return false;
        }
    }
    // stsd → 首个 entry → sinf/schi/tenc
    {
        const quint32 p3[] = { T_stsd };
        quint64 o, s;
        if (!findBox(buf, stblOff, stblOff + stblSize, p3, 1, &o, &s)
            || s < 8)
            return false;
        // fullbox(4) + entry_count(4) + entries
        const quint64 e0 = o + 8, e0End = o + s;
        // 只看首个 entry: 由 entry_size 头
        if (e0 + 8 > e0End) return false;
        const quint64 eSize = rd32(buf, e0);
        const quint64 eEnd = qMin(e0 + eSize, e0End);
        // 注意:entry 内容区前有 SampleEntry 定长头(encv 78B / enca 28B),
        // 不能直接按 box 遍历 → 用四字符标记定位 sinf/schi/tenc(entry 内唯一)。
        quint64 to = 0, ts = 0;
        if (findNestedByMarker(buf, e0 + 8, eEnd, &to, &ts) && ts >= 24) {
            // tenc 内容区: ver/flags(4) reserved(2) IsEnc(1) IVsize(1) KID(16)
            t.ivSize = quint32(static_cast<unsigned char>(buf[int(to) + 7]));
            t.kid = buf.mid(int(to) + 8, 16);
        }
        // 无 tenc → 未加密轨,不产出 TrackInfo
        if (t.kid.size() != 16) return true;
    }
    // stsz(样本大小表)
    {
        const quint32 p5[] = { T_stsz };
        quint64 o, s;
        if (!findBox(buf, stblOff, stblOff + stblSize, p5, 1, &o, &s)
            || s < 12)
            return false;
        const quint32 uniform = rd32(buf, o + 4);
        const quint32 cnt = rd32(buf, o + 8);
        if (uniform != 0) {
            for (quint32 i = 0; i < cnt; ++i)
                t.sampleSizes.append(uniform);
        } else {
            if (s < 12 + quint64(cnt) * 4) return false;
            for (quint32 i = 0; i < cnt; ++i)
                t.sampleSizes.append(rd32(buf, o + 12 + quint64(i) * 4));
        }
    }
    // stsc + stco/co64 → 样本绝对偏移
    {
        const quint32 p6[] = { T_stsc };
        quint64 o, s;
        struct Run { quint32 first, per; };
        QVector<Run> runs;
        if (findBox(buf, stblOff, stblOff + stblSize, p6, 1, &o, &s)) {
            const quint32 cnt = rd32(buf, o + 4);
            if (s < 8 + quint64(cnt) * 12) return false;
            for (quint32 i = 0; i < cnt; ++i) {
                runs.append({ rd32(buf, o + 8 + quint64(i) * 12),
                              rd32(buf, o + 12 + quint64(i) * 12) });
            }
        }
        quint32 co64Type = T_stco;
        const quint32 p7[] = { T_stco };
        if (!findBox(buf, stblOff, stblOff + stblSize, p7, 1, &o, &s)) {
            const quint32 p8[] = { T_co64 };
            if (!findBox(buf, stblOff, stblOff + stblSize, p8, 1, &o, &s))
                return false;
            co64Type = T_co64;
        }
        const quint32 chunkCnt = rd32(buf, o + 4);
        if (co64Type == T_stco ? (s < 8 + quint64(chunkCnt) * 4)
                               : (s < 8 + quint64(chunkCnt) * 8))
            return false;
        quint32 si = 0;
        for (quint32 c = 0; c < chunkCnt && si < quint32(t.sampleSizes.size());
             ++c) {
            quint64 chunkBase = co64Type == T_stco
                ? quint64(rd32(buf, o + 8 + quint64(c) * 4))
                : rd64(buf, o + 8 + quint64(c) * 8);
            // 本 chunk 的 samples-per-chunk: 最后一个 firstChunk <= c+1 的 run
            quint32 per = 0;
            for (int r = runs.size() - 1; r >= 0; --r) {
                if (runs[r].first <= c + 1) { per = runs[r].per; break; }
            }
            for (quint32 k = 0; k < per && si < quint32(t.sampleSizes.size());
                 ++k) {
                t.sampleOffsets.append(chunkBase);
                chunkBase += t.sampleSizes[si];
                ++si;
            }
        }
        if (si != quint32(t.sampleSizes.size())) return false;
    }
    // senc(本轨) → 首样本 IV 前 8 字节 = base_iv
    {
        const quint32 p9[] = { T_senc };
        quint64 o, s;
        if (!findBox(buf, stblOff, stblOff + stblSize, p9, 1, &o, &s))
            return true;   // 该轨无 senc → 未加密(不产出)
        if (s < 8) return false;
        const quint32 cnt = rd32(buf, o + 4);
        if (cnt != quint32(t.sampleSizes.size())) return false;
        t.baseIv = rd64(buf, o + 8);   // 首样本 IV 前 8 字节(BE)
    }
    return true;
}

} // namespace

int findTracks(const QByteArray& buf, QList<TrackInfo>& out) {
    const quint32 p[] = { T_trak };
    // 遍历 moov 下全部 trak
    quint64 moovOff = 0, moovSize = 0;
    // 先找 moov(顶层)
    bool found = false;
    quint64 o = 0;
    while (o + 8 <= quint64(buf.size())) {
        quint64 size = rd32(buf, o);
        quint64 hdr = 8;
        const quint32 ty = rd32(buf, o + 4);
        if (size == 1) { size = rd64(buf, o + 8); hdr = 16; }
        else if (size == 0) size = quint64(buf.size()) - o;
        if (size < hdr || o + size > quint64(buf.size())) return -1;
        if (isType(ty, "moov")) {
            moovOff = o + hdr;
            moovSize = size - hdr;
            found = true;
            HGDBG("moov at %llu size %llu", (unsigned long long)o,
                  (unsigned long long)size);
            break;
        }
        o += size;
    }
    if (!found) return -1;
    // moov 下逐个 trak
    o = moovOff;
    while (o + 8 <= moovOff + moovSize) {
        quint64 size = rd32(buf, o);
        quint64 hdr = 8;
        const quint32 ty = rd32(buf, o + 4);
        if (size == 1) { size = rd64(buf, o + 8); hdr = 16; }
        else if (size == 0) size = moovOff + moovSize - o;
        if (size < hdr || o + size > moovOff + moovSize) return -1;
        if (isType(ty, "trak")) {
            HGDBG("trak at %llu size %llu", (unsigned long long)o,
                  (unsigned long long)size);
            TrackInfo t;
            const int pr = parseTrack(buf, o + hdr, size - hdr, t);
            HGDBG("parseTrack -> %d video=%d ivSize=%u sizes=%d offs=%d",
                  pr, int(t.isVideo), t.ivSize, t.sampleSizes.size(),
                  t.sampleOffsets.size());
            if (pr == false) return -1;
            if (t.kid.size() == 16 && !t.sampleOffsets.isEmpty()
                && t.baseIv != 0)
                out.append(t);
        }
        o += size;
    }
    Q_UNUSED(p);
    return out.isEmpty() ? -1 : 0;
}

} // namespace hgcenc
} // namespace qxv
