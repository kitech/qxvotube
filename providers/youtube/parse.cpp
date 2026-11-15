#include "parse.h"

#include <functional>

namespace qxv {
namespace youtube {
namespace parse {

QList<VideoItem> searchItems(cJSON* root) {
    QList<VideoItem> items;
    // 通用递归遍历(结构随版本改版,不写死路径):
    //   WEB  → videoRenderer;ANDROID/IOS → compactVideoRenderer
    std::function<void(cJSON*)> walk = [&](cJSON* n) {
        if (!n) return;
        if (cJSON_IsObject(n)) {
            if (cJSON* vr = cJSON_GetObjectItemCaseSensitive(n, "videoRenderer")) {
                if (cJSON_IsObject(vr)) {
                    VideoItem vi;
                    vi.id = jstr(vr, "videoId");
                    cJSON* runs = jarr(jobj(vr, "title"), "runs");
                    vi.title = jstr(jat(runs, 0), "text");
                    runs = jarr(jobj(vr, "ownerText"), "runs");
                    vi.author = jstr(jat(runs, 0), "text");
                    cJSON* thumbs = jarr(jobj(vr, "thumbnail"), "thumbnails");
                    vi.cover = jstr(jat(thumbs, jsize(thumbs) - 1), "url");
                    if (!vi.id.isEmpty()) items.append(vi);
                }
            } else if (cJSON* cv = cJSON_GetObjectItemCaseSensitive(
                           n, "compactVideoRenderer")) {
                if (cJSON_IsObject(cv)) {
                    VideoItem vi;
                    vi.id = jstr(cv, "videoId");
                    const cJSON* title = jobj(cv, "title");
                    vi.title = jstr(title, "simpleText");
                    if (vi.title.isEmpty())
                        vi.title = jstr(jat(jarr(title, "runs"), 0), "text");
                    vi.author = jstr(jat(jarr(jobj(cv, "shortBylineText"),
                                               "runs"), 0), "text");
                    cJSON* thumbs = jarr(jobj(cv, "thumbnail"), "thumbnails");
                    vi.cover = jstr(jat(thumbs, jsize(thumbs) - 1), "url");
                    if (!vi.id.isEmpty()) items.append(vi);
                }
            }
            cJSON* ch;
            cJSON_ArrayForEach(ch, n) walk(ch);
        } else if (cJSON_IsArray(n)) {
            cJSON* ch;
            cJSON_ArrayForEach(ch, n) walk(ch);
        }
    };
    walk(root);
    return items;
}

QString continuation(cJSON* n) {
    if (!n) return QString();
    if (cJSON_IsObject(n)) {
        if (cJSON* cc = cJSON_GetObjectItemCaseSensitive(n, "continuationCommand")) {
            if (cJSON_IsObject(cc)) {
                const QString t = jstr(cc, "token");
                if (!t.isEmpty()) return t;
            }
        }
        const char* kOld[] = { "nextContinuationData", "reloadContinuationData" };
        for (const char* key : kOld) {
            if (cJSON* oc = cJSON_GetObjectItemCaseSensitive(n, key)) {
                if (cJSON_IsObject(oc)) {
                    const QString t = jstr(oc, "continuation");
                    if (!t.isEmpty()) return t;
                }
            }
        }
        cJSON* ch;
        cJSON_ArrayForEach(ch, n) {
            const QString t = continuation(ch);
            if (!t.isEmpty()) return t;
        }
    } else if (cJSON_IsArray(n)) {
        cJSON* ch;
        cJSON_ArrayForEach(ch, n) {
            const QString t = continuation(ch);
            if (!t.isEmpty()) return t;
        }
    }
    return QString();
}

QList<QString> playlistVideoIds(cJSON* n, int cap) {
    QList<QString> out;
    if (!n || cap <= 0) return out;
    if (cJSON_IsObject(n)) {
        if (cJSON* we = cJSON_GetObjectItemCaseSensitive(n, "watchEndpoint")) {
            if (cJSON_IsObject(we)) {
                const QString vid = jstr(we, "videoId");
                const QString pl = jstr(we, "playlistId");
                if (!vid.isEmpty() && !pl.isEmpty() && !out.contains(vid))
                    out.append(vid);
            }
        }
        cJSON* ch;
        cJSON_ArrayForEach(ch, n) {
            const QList<QString> sub = playlistVideoIds(ch, cap - out.size());
            for (int i = 0; i < sub.size(); ++i)
                if (!out.contains(sub[i])) out.append(sub[i]);
            if (out.size() >= cap) break;
        }
    } else if (cJSON_IsArray(n)) {
        cJSON* ch;
        cJSON_ArrayForEach(ch, n) {
            const QList<QString> sub = playlistVideoIds(ch, cap - out.size());
            for (int i = 0; i < sub.size(); ++i)
                if (!out.contains(sub[i])) out.append(sub[i]);
            if (out.size() >= cap) break;
        }
    }
    return out;
}

} // namespace parse
} // namespace youtube
} // namespace qxv
