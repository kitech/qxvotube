#include <stdint.h>
#include "cenc.h"

#include <openssl/evp.h>

namespace qxv {
namespace hgcenc {

bool Cenc::nalOk(const char* p, int sz) {
    int pos = 0;
    while (pos + 4 <= sz) {
        const uint32_t ln = (uint32_t(static_cast<unsigned char>(p[pos])) << 24)
                         | (uint32_t(static_cast<unsigned char>(p[pos + 1])) << 16)
                         | (uint32_t(static_cast<unsigned char>(p[pos + 2])) << 8)
                         |  uint32_t(static_cast<unsigned char>(p[pos + 3]));
        if (ln == 0 || pos + 4 + int(ln) > sz) return false;
        pos += 4 + int(ln);
    }
    return pos == sz;
}

int Cenc::decryptTrack(QByteArray& buf, const TrackInfo& track,
                       const QByteArray& key) {
    if (key.size() != 16 || track.baseIv == 0) return -1;
    const int n = track.sampleOffsets.size();
    if (n != track.sampleSizes.size() || n == 0) return -2;
    int ok = 0;
    for (int idx = 0; idx < n; ++idx) {
        const uint64_t off = track.sampleOffsets[idx];
        const int sz = int(track.sampleSizes[idx]);
        if (off + uint64_t(sz) > uint64_t(buf.size())) return -3;
        // IV = ((baseIv + idx) << 64): 高 8 字节计数器,低 8 字节块内偏移 0
        uint64_t ctr = track.baseIv + uint64_t(idx);
        unsigned char iv[16];
        for (int i = 0; i < 8; ++i)
            iv[i] = static_cast<unsigned char>(ctr >> (8 * (7 - i)));
        for (int i = 8; i < 16; ++i) iv[i] = 0;

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return -4;
        unsigned char* dst = reinterpret_cast<unsigned char*>(
            buf.data() + int(off));
        const unsigned char* src = reinterpret_cast<const unsigned char*>(
            buf.data() + int(off));
        int outLen = 0, total = 0;
        bool fail = false;
        if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr,
                               reinterpret_cast<const unsigned char*>(
                                   key.data()),
                               iv) != 1)
            fail = true;
        if (!fail
            && EVP_DecryptUpdate(ctx, dst, &outLen, src, sz) != 1)
            fail = true;
        else
            total = outLen;
        if (!fail && total != sz) fail = true;
        EVP_CIPHER_CTX_free(ctx);
        if (fail) return -5;
        ++ok;
    }
    return ok;
}

} // namespace hgcenc
} // namespace qxv
