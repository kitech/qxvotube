#pragma once
#include "compat.h"
#include "result.h"

#include <qdatetime.h>
#include <qmap.h>
#include <qstring.h>

// 直链缓存(TTL):CDN 直链带时效(红果 ~6h / bilibili ~2h / YouTube expire=...),
// 过期后必须重新 resolve,否则播放器报 403。
// 用法:
//   PlayResult pr;
//   if (m_cache.get(key, &pr)) { cb(Result::ok(), pr); return; }   // 命中
//   ... resolve ...
//   m_cache.put(key, pr, qxv::streamcache::ttlFromUrl(pr.streams[0].url,
//                                                   30 * 60 * 1000));
namespace qxv {
namespace streamcache {

inline qint64 nowMs() { return wallMs(); }

// 从 URL 查询串解析 expire=<unix 秒>(YouTube googlevideo 等),
// 返回 min(fallbackMs, expire-now-60s);解析不到/已过期返回 0。
inline qint64 ttlFromUrl(const QString& url, qint64 fallbackMs) {
    qint64 ttl = fallbackMs;
    const int i = qIndexOf(url, qFromUtf8("expire="));
    if (i >= 0) {
        int j = i + 7;
        QString digits;
        while (j < url.length()) {
            const QChar c = url.at(j);
            if (c >= QChar('0') && c <= QChar('9')) { digits.append(c); ++j; }
            else break;
        }
        bool ok = false;
        const qint64 sec = digits.toLongLong(&ok);
        if (ok && sec > 0) {
            const qint64 left = sec * 1000 - nowMs() - 60 * 1000;
            if (left <= 0) return 0;
            if (left < ttl) ttl = left;
        }
    }
    return ttl > 0 ? ttl : 0;
}

class Cache {
public:
    bool get(const QString& key, PlayResult* out) {
        QMap<QString, Entry>::iterator it = m_.find(key);
        if (it == m_.end()) return false;
        if (nowMs() >= (*it).expiresAt) { m_.erase(it); return false; }
        if (out) *out = (*it).pr;
        return true;
    }

    void put(const QString& key, const PlayResult& pr, qint64 ttlMs) {
        if (ttlMs <= 0 || pr.streams.isEmpty()) return;
        if (m_.size() > 32) m_.clear();   // 粗暴上限,防长跑累积
        Entry e;
        e.pr = pr;
        e.expiresAt = nowMs() + ttlMs;
        m_[key] = e;
    }

    void clear() { m_.clear(); }
    int size() const { return m_.size(); }

private:
    struct Entry {
        PlayResult pr;
        qint64 expiresAt = 0;
    };
    QMap<QString, Entry> m_;
};

} // namespace streamcache
} // namespace qxv
