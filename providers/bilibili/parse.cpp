#include "parse.h"

namespace qxv {
namespace bilibili {
namespace parse {

QList<VideoItem> searchResult(cJSON* root) {
    QList<VideoItem> out;
    cJSON* results = jarr(jobj(root, "data"), "result");
    for (int i = 0; i < jsize(results); ++i) {
        const cJSON* g = jat(results, i);          // result_type 分组
        if (jstr(g, "result_type") != qFromUtf8("video")) continue;
        cJSON* data = jarr(g, "data");
        for (int k = 0; k < jsize(data); ++k) {
            const cJSON* it = jat(data, k);
            VideoItem vi;
            vi.id     = jstr(it, "bvid");
            vi.title  = jstr(it, "title");
            vi.author = jstr(it, "author");
            vi.uid    = QString::number(jint(it, "mid"));
            vi.cover  = jstr(it, "pic");
            vi.durationMs = jint(it, "duration") * 1000;
            if (!vi.id.isEmpty()) out.append(vi);
        }
    }
    return out;
}

Result detailFromView(cJSON* root, VideoItem* viOut, QList<VideoItem>* partsOut) {
    const cJSON* data = jobj(root, "data");
    if (!data) return Result::fail(Err::Parse, qFromUtf8("bilibili: no data"));
    VideoItem vi;
    vi.id     = jstr(data, "bvid");
    vi.title  = jstr(data, "title");
    vi.author = jstr(jobj(data, "owner"), "name");
    vi.uid    = QString::number(jint(jobj(data, "owner"), "mid"));
    vi.cover  = jstr(data, "pic");
    vi.durationMs = jint(data, "duration") * 1000;
    vi.extra.insert(qFromUtf8("desc"), jstr(data, "desc"));
    vi.extra.insert(qFromUtf8("tname"), jstr(data, "tname"));
    vi.extra.insert(qFromUtf8("pubdate"), (long long)jint(data, "pubdate"));
    vi.extra.insert(qFromUtf8("view"),
                    (long long)jint(jobj(data, "stat"), "view"));
    vi.extra.insert(qFromUtf8("cid"), QString::number(jint(data, "cid")));

    // 分P:part.id 用 "bvid.cid" 复合形式 → 可直接喂 resolve/resolveQuality
    QList<VideoItem> parts;
    cJSON* pages = jarr(data, "pages");
    for (int i = 0; i < jsize(pages); ++i) {
        const cJSON* p = jat(pages, i);
        VideoItem part;
        const QString cid = QString::number(jint(p, "cid"));
        part.id = qFromUtf8("%1.%2").arg(vi.id, cid);
        part.title = jstr(p, "part");
        part.durationMs = jint(p, "duration") * 1000;
        part.extra = QVariantMap{
            { qFromUtf8("bvid"), vi.id },
            { qFromUtf8("cid"), cid },
            { qFromUtf8("page"), (long long)jint(p, "page") },
        };
        parts.append(part);
    }
    if (viOut) *viOut = vi;
    if (partsOut) *partsOut = parts;
    return Result::ok();
}

QList<VideoItem> popular(cJSON* root, bool* hasMore) {
    const cJSON* data = jobj(root, "data");
    QList<VideoItem> items;
    cJSON* list = jarr(data, "list");
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        VideoItem vi;
        vi.id     = jstr(it, "bvid");
        vi.title  = jstr(it, "title");
        vi.author = jstr(jobj(it, "owner"), "name");
        vi.uid    = QString::number(jint(jobj(it, "owner"), "mid"));
        vi.cover  = jstr(it, "pic");
        vi.durationMs = jint(it, "duration") * 1000;
        vi.extra.insert(qFromUtf8("aid"), (long long)jint(it, "aid"));
        vi.extra.insert(qFromUtf8("tname"), jstr(it, "tname"));
        if (!vi.id.isEmpty()) items.append(vi);
    }
    if (hasMore) *hasMore = !jbool(data, "no_more");
    return items;
}

QList<VideoItem> pageList(cJSON* root, const QString& bvid) {
    QList<VideoItem> parts;
    cJSON* list = jarr(root, "data");
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* p = jat(list, i);
        VideoItem part;
        const QString cid = QString::number(jint(p, "cid"));
        part.id = qFromUtf8("%1.%2").arg(bvid, cid);
        part.title = jstr(p, "part");
        part.durationMs = jint(p, "duration") * 1000;
        part.extra = QVariantMap{
            { qFromUtf8("bvid"), bvid },
            { qFromUtf8("cid"), cid },
            { qFromUtf8("page"), (long long)jint(p, "page") },
        };
        parts.append(part);
    }
    return parts;
}

} // namespace parse
} // namespace bilibili
} // namespace qxv
