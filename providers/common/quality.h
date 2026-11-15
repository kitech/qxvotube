#pragma once
#include "compat.h"

#include <QString>
#include <QStringList>

// 全仓统一的清晰度词汇(2026-11-15):
//   144/160/240/360/480/540/720/1080/1440/2160(大小写不敏感,"720p"/"720P" 等价)
// 语义:
//   - 空串/"auto"/"best" → 0(provider 默认最高可播档)
//   - 无法识别 → -1(调用方报 Err::Unsupported)
// 降级约定:实际档与请求档不一致时,Stream.extra 写
//   requestedQuality(原样) + actualQuality(实际) + degraded=true
//   —— 包括"请求更低但实际给了更高"(省流量意图未满足)的情况。
namespace qxv {
namespace quality {

inline int heightOf(const QString& q) {
    const QString s = q.trimmed().toLower();
    if (s.isEmpty() || s == qFromUtf8("auto") || s == qFromUtf8("best"))
        return 0;
    QString digits;
    for (int i = 0; i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (ch >= QChar('0') && ch <= QChar('9')) digits.append(ch);
        else break;
    }
    if (digits.isEmpty()) return -1;
    bool ok = false;
    const int h = digits.toInt(&ok);
    return ok ? h : -1;
}

inline QString labelFor(int height) {
    return height > 0 ? qFromUtf8("%1P").arg(height) : QString();
}

// 从可用档里挑"≤请求高度的最高档";都高于请求则返回最低档(并应标 degraded)。
// wantHeight<=0 表示不限制(取最高)。返回空 = avail 无有效档。
inline QString bestAtMost(const QStringList& avail, int wantHeight) {
    int best = -1, lowest = -1;
    for (int i = 0; i < avail.size(); ++i) {
        const int h = heightOf(avail.at(i));
        if (h <= 0) continue;
        if (lowest < 0 || h < lowest) lowest = h;
        if (wantHeight <= 0 || h <= wantHeight) {
            if (h > best) best = h;
        }
    }
    if (best < 0) best = lowest;
    return labelFor(best);
}

} // namespace quality
} // namespace qxv
