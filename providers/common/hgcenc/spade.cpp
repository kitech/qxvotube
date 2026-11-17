#include "spade.h"

#include <qvector.h>

// 口径与 zhangbaio/hongguo frida/unwrap_spade.py 逐行对齐:
//   bVar5 = spade[0]^spade[1]^spade[2]      初级种子
//   typeLen = bVar5 - 0x30                 type 字符串长度(在尾部)
//   work = spade[1 .. 1+workLen)           工作缓冲
//   type 串: s1[i] = b14^b16^spade[L-typeLen+i](b14/b16 取自尾前两个字节)
//   ver1 变换: 逐字节,XOR 状态对(b14,b16)按奇偶轮转交换,
//              位移量 = flag? (popcount(i)+0x15) : s8(-0x15-popcount(i)),
//              dest[i] = (shift + (b16 ^ b6)) & 0xff
//   key 长度前缀: dest[0] = '0'..'9' → 减 0x30; 'a'..'z' → 减 0x57;
//                 key = dest[1 .. workLen-n)

namespace qxv {
namespace hgcenc {

namespace {

inline int popcount32(unsigned x) {
    int n = 0;
    while (x) { x &= x - 1; ++n; }
    return n;
}

// C 的 signed char 截断语义(负位移用)
inline int s8(int v) {
    v &= 0xff;
    return v >= 128 ? v - 256 : v;
}

// strncmp 口径:任一不等为假;前缀含 '\0' 即真(模拟 C 字符串比较提前终止)
bool strncmp0(const QByteArray& a, const char* b, int n) {
    for (int k = 0; k < n; ++k) {
        const char ca = k < a.size() ? a[k] : '\0';
        const char cb = b[k];
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
    return true;
}

} // namespace

QString Spade::unwrapV1(const QByteArray& spade, int flag) {
    const int L = spade.size();
    if (L < 3) return QString();
    const int bVar5 = static_cast<unsigned char>(spade[0])
                    ^ static_cast<unsigned char>(spade[1])
                    ^ static_cast<unsigned char>(spade[2]);
    const int typeLen = bVar5 - 0x30;          // type 字符串长度
    if (typeLen < 1) return QString();
    const int workLen = (L - bVar5) + 0x2f;    // 工作缓冲长度
    if (workLen < 1 || 1 + workLen > L) return QString();
    QVector<quint8> dest(workLen);
    for (int i = 0; i < workLen; ++i)
        dest[i] = static_cast<unsigned char>(spade[1 + i]);

    // 解出尾部 type 字符串,判 v1/v2
    {
        const int b16 = static_cast<unsigned char>(spade[L - typeLen - 2]);
        const int b14 = static_cast<unsigned char>(spade[L - typeLen - 1]);
        QByteArray type(typeLen, '\0');
        for (int i = 0; i < typeLen; ++i) {
            const int c = static_cast<unsigned char>(spade[i + (L - typeLen)]);
            type[i] = static_cast<char>(b14 ^ b16 ^ c);
        }
        if (strncmp0(type, "app_v2", typeLen) || strncmp0(type, "web_v2", typeLen))
            return QString();   // ver2: AES-GCM 路径,不实现不静默猜
    }

    // ver1 字节变换
    int b14 = 0x55, b16 = 0xfa;
    for (int i = 0; i < workLen; ++i) {
        const quint8 b6 = dest[i];
        const int u18 = popcount32(static_cast<unsigned>(i));
        int b3 = b6, b7 = b14;
        if (i & 1) { b3 = b16; b7 = b6; b16 = b14; }
        const int cVar4 = flag ? (u18 + 0x15) : s8(-0x15 - u18);
        dest[i] = static_cast<quint8>((cVar4 + (b16 ^ b6)) & 0xff);
        b14 = b7;
        b16 = b3;
    }

    // 按 dest[0] 的长度前缀切出 content key(32 hex)
    const int b0 = dest[0];
    int n = 0;
    if (b0 >= 0x30 && b0 <= 0x39) n = b0 - 0x30;
    else if (b0 >= 0x61 && b0 <= 0x7a) n = b0 - 0x57;
    else return QString();
    const int keyLen = workLen - n;
    if (keyLen < 2) return QString();

    // dest[1..keyLen) 即 32 hex 字符(直接作为返回;不逐位转 hex——
    // 依据 Python 实现 bytes(dest[1:iv9]).decode("latin1") 即 hex 字符串本身)
    QByteArray out;
    out.reserve(keyLen - 1);
    for (int i = 1; i < keyLen; ++i)
        out.append(static_cast<char>(dest[i]));
    const QString key = QString::fromLatin1(out);
    // 结构自证: 必须是 32 hex
    if (key.size() != 32) return QString();
    for (const QChar ch : key)
        if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f')
            && !(ch >= 'A' && ch <= 'F'))
            return QString();
    return key;
}

} // namespace hgcenc
} // namespace qxv
