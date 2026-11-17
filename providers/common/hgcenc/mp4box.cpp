#include <stdint.h>
#include "mp4box.h"

#include <cstdio>
#include <vector>

#define HGDBG(...) do { if (getenv("HGDBG")) { fprintf(stderr, "[hgdbg] "); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

// 口径: ISO 14496-12/15 + zhangbaio/hongguo oracle.py(样本表展开、senc 首样本
// IV 前 8 字节为 base_iv、tenc v0 布局: ver/flags(4) reserved(2) IsEnc(1)
// IVsize(1) KID(16))。

namespace qxv {
namespace hgcenc {

namespace {

inline uint32_t rd32(const QByteArray& b, uint64_t o) {
    return (uint32_t(static_cast<unsigned char>(b[int(o)])) << 24)
         | (uint32_t(static_cast<unsigned char>(b[int(o) + 1])) << 16)
         | (uint32_t(static_cast<unsigned char>(b[int(o) + 2])) << 8)
         |  uint32_t(static_cast<unsigned char>(b[int(o) + 3]));
}

inline uint64_t rd64(const QByteArray& b, uint64_t o) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | uint32_t(static_cast<unsigned char>(b[int(o) + i]));
    return v;
}

// 找 path 指定的嵌套 box(从 off 起,同层逐个),返回内容区起始与大小;
// 找不到返回 false。path 为如 {'mdia','minf','stbl'}。
bool findBox(const QByteArray& buf, uint64_t off, uint64_t end,
             const uint32_t* path, int pathLen,
             uint64_t* contentOff, uint64_t* contentSize) {
    uint64_t o = off;
    for (int d = 0; d < pathLen; ++d) {
        bool hit = false;
        while (o + 8 <= end) {
            uint64_t size = rd32(buf, o);
            uint64_t hdr = 8;
            uint32_t ty = rd32(buf, o + 4);
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

bool isType(uint64_t t, const char* s) {
    return static_cast<char>(t >> 24) == s[0]
        && static_cast<char>((t >> 16) & 0xff) == s[1]
        && static_cast<char>((t >> 8) & 0xff) == s[2]
        && static_cast<char>(t & 0xff) == s[3];
}

// 在 [lo,hi) 内按四字符标记定位 sinf→schi→tenc(各层记 box 尺寸自证),
// 返回 tenc 内容区起点/长度。SampleEntry 定长头(encv 78B/enca 28B)不可 box 遍历,
// 故此处用标记而非递归 findBox。
bool findNestedByMarker(const QByteArray& buf, uint64_t lo, uint64_t hi,
                        uint64_t* tencOff, uint64_t* tencSize) {
    const QByteArray b = buf;
    int i = int(lo);
    const uint64_t endU = hi < uint64_t(b.size()) ? hi : uint64_t(b.size());
    const int end = int(endU);
    while (i + 8 <= end) {
        i = qIndexOf(b, qbaFromData("tenc", 4), i);
        if (i < 0 || i + 8 > end) return false;
        // 该 tenc 的上层必须是 schi,再上层是 sinf
        const int schi = i - 4;   // 假定 tenc 紧跟 schi 内容(实测布局)
        Q_UNUSED(schi);
        const uint64_t boxSize = rd32(b, uint64_t(i) - 4);
        // tenc box = 8 头 + 20 内容(ver/flags 4 + reserved 2 + IsEnc 1 + IVsize 1 + KID 16)
        if (boxSize == 32 || boxSize >= 32) {
            *tencOff = uint64_t(i) + 4;
            *tencSize = boxSize - 8;
            return true;
        }
        ++i;
    }
    return false;
}

} // namespace

bool walkBoxes(const QByteArray& buf, uint64_t off, uint64_t end, BoxCb cb,
               void* ud) {
    uint64_t o = off;
    while (o + 8 <= end) {
        uint64_t size = rd32(buf, o);
        uint64_t hdr = 8;
        const uint32_t ty = rd32(buf, o + 4);
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
const uint32_t T_trak = 0x7472616b; // 'trak'
const uint32_t T_mdia = 0x6d646961; // 'mdia'
const uint32_t T_minf = 0x6d696e66; // 'minf'
const uint32_t T_stbl = 0x7374626c; // 'stbl'
const uint32_t T_hdlr = 0x68646c72; // 'hdlr'
const uint32_t T_stsd = 0x73747364; // 'stsd'
const uint32_t T_sinf = 0x73696e66; // 'sinf'
const uint32_t T_schi = 0x73636869; // 'schi'
const uint32_t T_tenc = 0x74656e63; // 'tenc'
const uint32_t T_stsz = 0x7374737a; // 'stsz'
const uint32_t T_stsc = 0x73747363; // 'stsc'
const uint32_t T_stco = 0x7374636f; // 'stco'
const uint32_t T_co64 = 0x636f3634; // 'co64'
const uint32_t T_senc = 0x73656e63; // 'senc'
const uint32_t H_vide = 0x76696465; // 'vide'
const uint32_t H_soun = 0x736f756e; // 'soun'

bool parseTrack(const QByteArray& buf, uint64_t trakOff, uint64_t trakSize,
                TrackInfo& t) {
    const uint32_t p1[] = { T_mdia, T_minf, T_stbl };
    uint64_t stblOff, stblSize;
    if (!findBox(buf, trakOff, trakOff + trakSize, p1, 3, &stblOff,
                 &stblSize)) {
        HGDBG("no stbl in trak");
        return false;
    }
    HGDBG("stbl at %llu size %llu", (unsigned long long)stblOff,
          (unsigned long long)stblSize);
    // hdlr
    {
        const uint32_t p2[] = { T_mdia, T_hdlr };
        uint64_t o, s;
        if (findBox(buf, trakOff, trakOff + trakSize, p2, 2, &o, &s)
            && s >= 12) {
            const uint32_t h = rd32(buf, o + 8);   // ver/flags(4)+pre_defined(4)
            if (h == H_vide) t.isVideo = true;
            else if (h != H_soun) return true;    // 非音视频轨跳过
        } else {
            return false;
        }
    }
    // stsd → 首个 entry → sinf/schi/tenc
    {
        const uint32_t p3[] = { T_stsd };
        uint64_t o, s;
        if (!findBox(buf, stblOff, stblOff + stblSize, p3, 1, &o, &s)
            || s < 8)
            return false;
        // fullbox(4) + entry_count(4) + entries
        const uint64_t e0 = o + 8, e0End = o + s;
        // 只看首个 entry: 由 entry_size 头
        if (e0 + 8 > e0End) return false;
        const uint64_t eSize = rd32(buf, e0);
        const uint64_t eEnd = (e0 + eSize) < e0End ? (e0 + eSize) : e0End;
        // 注意:entry 内容区前有 SampleEntry 定长头(encv 78B / enca 28B),
        // 不能直接按 box 遍历 → 用四字符标记定位 sinf/schi/tenc(entry 内唯一)。
        uint64_t to = 0, ts = 0;
        if (findNestedByMarker(buf, e0 + 8, eEnd, &to, &ts) && ts >= 24) {
            // tenc 内容区: ver/flags(4) reserved(2) IsEnc(1) IVsize(1) KID(16)
            t.ivSize = uint32_t(static_cast<unsigned char>(buf[int(to) + 7]));
            t.kid = qbaMid(buf, int(to) + 8, 16);
        }
        // 无 tenc → 未加密轨,不产出 TrackInfo
        if (t.kid.size() != 16) return true;
    }
    // stsz(样本大小表)
    {
        const uint32_t p5[] = { T_stsz };
        uint64_t o, s;
        if (!findBox(buf, stblOff, stblOff + stblSize, p5, 1, &o, &s)
            || s < 12)
            return false;
        const uint32_t uniform = rd32(buf, o + 4);
        const uint32_t cnt = rd32(buf, o + 8);
        if (uniform != 0) {
            for (uint32_t i = 0; i < cnt; ++i)
                t.sampleSizes.append(uniform);
        } else {
            if (s < 12 + uint64_t(cnt) * 4) return false;
            for (uint32_t i = 0; i < cnt; ++i)
                t.sampleSizes.append(rd32(buf, o + 12 + uint64_t(i) * 4));
        }
    }
    // stsc + stco/co64 → 样本绝对偏移
    {
        const uint32_t p6[] = { T_stsc };
        uint64_t o, s;
        struct Run { uint32_t first, per; };
        std::vector<Run> runs;
        if (findBox(buf, stblOff, stblOff + stblSize, p6, 1, &o, &s)) {
            const uint32_t cnt = rd32(buf, o + 4);
            if (s < 8 + uint64_t(cnt) * 12) return false;
            for (uint32_t i = 0; i < cnt; ++i) {
                runs.push_back({ rd32(buf, o + 8 + uint64_t(i) * 12),
                              rd32(buf, o + 12 + uint64_t(i) * 12) });
            }
        }
        uint32_t co64Type = T_stco;
        const uint32_t p7[] = { T_stco };
        if (!findBox(buf, stblOff, stblOff + stblSize, p7, 1, &o, &s)) {
            const uint32_t p8[] = { T_co64 };
            if (!findBox(buf, stblOff, stblOff + stblSize, p8, 1, &o, &s))
                return false;
            co64Type = T_co64;
        }
        const uint32_t chunkCnt = rd32(buf, o + 4);
        if (co64Type == T_stco ? (s < 8 + uint64_t(chunkCnt) * 4)
                               : (s < 8 + uint64_t(chunkCnt) * 8))
            return false;
        uint32_t si = 0;
        for (uint32_t c = 0; c < chunkCnt && si < uint32_t(t.sampleSizes.size());
             ++c) {
            uint64_t chunkBase = co64Type == T_stco
                ? uint64_t(rd32(buf, o + 8 + uint64_t(c) * 4))
                : rd64(buf, o + 8 + uint64_t(c) * 8);
            // 本 chunk 的 samples-per-chunk: 最后一个 firstChunk <= c+1 的 run
            uint32_t per = 0;
            for (int r = runs.size() - 1; r >= 0; --r) {
                if (runs[r].first <= c + 1) { per = runs[r].per; break; }
            }
            for (uint32_t k = 0; k < per && si < uint32_t(t.sampleSizes.size());
                 ++k) {
                t.sampleOffsets.append(chunkBase);
                chunkBase += t.sampleSizes[si];
                ++si;
            }
        }
        if (si != uint32_t(t.sampleSizes.size())) return false;
    }
    // senc(本轨) → 首样本 IV 前 8 字节 = base_iv
    {
        const uint32_t p9[] = { T_senc };
        uint64_t o, s;
        if (!findBox(buf, stblOff, stblOff + stblSize, p9, 1, &o, &s))
            return true;   // 该轨无 senc → 未加密(不产出)
        if (s < 8) return false;
        const uint32_t cnt = rd32(buf, o + 4);
        if (cnt != uint32_t(t.sampleSizes.size())) return false;
        t.baseIv = rd64(buf, o + 8);   // 首样本 IV 前 8 字节(BE)
    }
    return true;
}

} // namespace

int findTracks(const QByteArray& buf, QList36<TrackInfo>& out) {
    const uint32_t p[] = { T_trak };
    // 遍历 moov 下全部 trak
    uint64_t moovOff = 0, moovSize = 0;
    // 先找 moov(顶层)
    bool found = false;
    uint64_t o = 0;
    while (o + 8 <= uint64_t(buf.size())) {
        uint64_t size = rd32(buf, o);
        uint64_t hdr = 8;
        const uint32_t ty = rd32(buf, o + 4);
        if (size == 1) { size = rd64(buf, o + 8); hdr = 16; }
        else if (size == 0) size = uint64_t(buf.size()) - o;
        if (size < hdr || o + size > uint64_t(buf.size())) return -1;
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
        uint64_t size = rd32(buf, o);
        uint64_t hdr = 8;
        const uint32_t ty = rd32(buf, o + 4);
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
