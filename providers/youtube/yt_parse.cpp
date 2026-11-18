#include "parse.h"

#include <functional>

namespace qxv {
namespace youtube {
namespace parse {

// videoRenderer(WEB/Live)→ VideoItem(搜索与分类共用字段口径)
static void fromVideoRenderer(cJSON* vr, VideoItem* vi) {
    vi->id = jstr(vr, "videoId");
    cJSON* runs = jarr(jobj(vr, "title"), "runs");
    vi->title = jstr(jat(runs, 0), "text");
    runs = jarr(jobj(vr, "ownerText"), "runs");
    vi->author = jstr(jat(runs, 0), "text");
    cJSON* thumbs = jarr(jobj(vr, "thumbnail"), "thumbnails");
    vi->cover = jstr(jat(thumbs, jsize(thumbs) - 1), "url");
    if (!vi->id.isEmpty())
        vi->webUrl = qFromUtf8("https://www.youtube.com/watch?v=") + vi->id;
}

QList36<VideoItem> searchItems(cJSON* root) {
    QList36<VideoItem> items;
    // 通用递归遍历(结构随版本改版,不写死路径):
    //   WEB  → videoRenderer;ANDROID/IOS → compactVideoRenderer
    std::function<void(cJSON*)> walk = [&](cJSON* n) {
        if (!n) return;
        if (cJSON_IsObject(n)) {
            if (cJSON* vr = cJSON_GetObjectItemCaseSensitive(n, "videoRenderer")) {
                if (cJSON_IsObject(vr)) {
                    VideoItem vi;
                    // M7:网页地址;publishedTimeText 是自然语言("2 days ago"),
                    // 不解析成时间戳 → publishMs 留 0,UI 显示 --
                    fromVideoRenderer(vr, &vi);
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

QList36<CategoryItem> guideItems(cJSON* root) {
    QList36<CategoryItem> out;
    std::function<void(cJSON*)> walk = [&](cJSON* n) {
        if (!n) return;
        if (cJSON_IsObject(n)) {
            if (cJSON* ge = cJSON_GetObjectItemCaseSensitive(
                    n, "guideEntryRenderer")) {
                if (cJSON_IsObject(ge)) {
                    cJSON* be = jobj(jobj(ge, "navigationEndpoint"),
                                     "browseEndpoint");
                    const QString id = jstr(be, "browseId");
                    // 只留真实主题频道(UC...);FE*/SP* 是页面导航/设置
                    if (id.startsWith(qFromUtf8("UC"))) {
                        cJSON* ft = jobj(ge, "formattedTitle");
                        QString title = jstr(ft, "simpleText");
                        if (title.isEmpty()) {
                            cJSON* runs = jarr(ft, "runs");
                            title = jstr(jat(runs, 0), "text");
                        }
                        const QString params = jstr(be, "params");
                        const QString key = params.isEmpty()
                            ? id : id + qFromUtf8("|") + params;
                        bool dup = false;
                        for (int i = 0; i < out.size(); ++i)
                            if (out[i].key == key) { dup = true; break; }
                        if (!title.isEmpty() && !dup)
                            out.append(CategoryItem(key, title));
                    }
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
    return out;
}

// lockupViewModel 的封面:sources[] 取最大宽度(末项)
static QString lockupCover(cJSON* lv) {
    cJSON* img = jobj(lv, "contentImage");
    cJSON* sources = jarr(jobj(jobj(img, "thumbnailViewModel"), "image"),
                          "sources");
    if (jsize(sources) == 0) {   // 合集卡:collectionThumbnailViewModel 分支
        cJSON* col = jobj(img, "collectionThumbnailViewModel");
        sources = jarr(jobj(jobj(jobj(col, "primaryThumbnail"),
                                 "thumbnailViewModel"), "image"), "sources");
    }
    return jstr(jat(sources, jsize(sources) - 1), "url");
}

QList36<VideoItem> browseItems(cJSON* root) {
    QList36<VideoItem> items;
    std::function<void(cJSON*)> walk = [&](cJSON* n) {
        if (!n) return;
        if (cJSON_IsObject(n)) {
            if (cJSON* vr = cJSON_GetObjectItemCaseSensitive(n, "videoRenderer")) {
                if (cJSON_IsObject(vr)) {
                    VideoItem vi;
                    fromVideoRenderer(vr, &vi);
                    if (!vi.id.isEmpty()) items.append(vi);
                }
            } else if (cJSON* vc = cJSON_GetObjectItemCaseSensitive(
                           n, "videoCardRenderer")) {
                if (cJSON_IsObject(vc)) {
                    VideoItem vi;
                    vi.id = jstr(vc, "videoId");
                    vi.title = jstr(jat(jarr(jobj(vc, "title"), "runs"), 0),
                                    "text");
                    vi.author = jstr(jat(jarr(jobj(vc, "bylineText"), "runs"), 0),
                                     "text");
                    cJSON* thumbs = jarr(jobj(vc, "thumbnail"), "thumbnails");
                    vi.cover = jstr(jat(thumbs, jsize(thumbs) - 1), "url");
                    if (!vi.id.isEmpty())
                        vi.webUrl = qFromUtf8("https://www.youtube.com/watch?v=")
                                    + vi.id;
                    if (!vi.id.isEmpty()) items.append(vi);
                }
            } else if (cJSON* lv = cJSON_GetObjectItemCaseSensitive(
                           n, "lockupViewModel")) {
                // 只收视频条目;playlist/album 条目(contentId 是 RD... 播放列表)
                // 不是视频 id,必须跳过,否则 resolve 必然 NotFound
                if (cJSON_IsObject(lv)
                    && jstr(lv, "contentType")
                       == qFromUtf8("LOCKUP_CONTENT_TYPE_VIDEO")) {
                    VideoItem vi;
                    vi.id = jstr(lv, "contentId");
                    cJSON* lmd = jobj(jobj(lv, "metadata"),
                                      "lockupMetadataViewModel");
                    vi.title = jstr(jobj(lmd, "title"), "content");
                    cJSON* rows = jarr(jobj(jobj(lmd, "metadata"),
                                            "contentMetadataViewModel"),
                                       "metadataRows");
                    vi.author = jstr(jobj(jat(jarr(jat(rows, 0),
                                                    "metadataParts"), 0), "text"),
                                     "content");
                    vi.cover = lockupCover(lv);
                    if (!vi.id.isEmpty())
                        vi.webUrl = qFromUtf8("https://www.youtube.com/watch?v=")
                                    + vi.id;
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

QList36<QString> playlistVideoIds(cJSON* n, int cap) {
    QList36<QString> out;
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
            const QList36<QString> sub = playlistVideoIds(ch, cap - out.size());
            for (int i = 0; i < sub.size(); ++i)
                if (!out.contains(sub[i])) out.append(sub[i]);
            if (out.size() >= cap) break;
        }
    } else if (cJSON_IsArray(n)) {
        cJSON* ch;
        cJSON_ArrayForEach(ch, n) {
            const QList36<QString> sub = playlistVideoIds(ch, cap - out.size());
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
