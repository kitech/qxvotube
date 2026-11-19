#include "parse.h"

#include <qmap.h>

namespace qxv {
namespace bilibili {
namespace parse {

// 搜索接口 title 带高亮标签(<em class="keyword">..</em>):剥掉
static QString stripTags(const QString& s) {
    QString out;
    bool inTag = false;
    for (int i = 0; i < s.length(); ++i) {
        const QChar c = s.at(i);
        if (c == QChar('<')) { inTag = true; continue; }
        if (c == QChar('>')) { inTag = false; continue; }
        if (!inTag) out.append(c);
    }
    return out;
}

// B站部分接口返回协议相对封面(//i0.hdslb.com/...):统一补 https:
static QString picUrl(const QString& u) {
    if (u.startsWith(qFromUtf8("//"))) return qFromUtf8("https:") + u;
    return u;
}

// 搜索接口 duration 是 "M:SS"/"H:MM:SS" 字符串(如 "3:36"),而列表接口是数字秒。
// 这里按冒号逐段累计成毫秒;空/非法返回 0(UI 侧按 0 隐藏时长,不自造数据)。
// 用跨版本 qIndexOf/mid/toInt,不用 Qt4+ 才有的 QString::split。
static qint64 durationTextMs(const QString& s) {
    if (s.isEmpty()) {
        return 0;
    }
    const QString sep = qFromUtf8(":");
    qint64 secs = 0;
    int from = 0;
    while (true) {
        const int colon = qIndexOf(s, sep, from);
        if (colon < 0) {
            break;
        }
        bool ok = false;
        const int v = s.mid(from, colon - from).toInt(&ok);
        if (!ok || v < 0) {
            return 0;
        }
        secs = secs * 60 + v; // 每段统一 ×60 进位:分→秒→时
        from = colon + 1;
    }
    bool ok = false;
    const int last = s.mid(from).toInt(&ok);
    if (!ok || last < 0) {
        return 0;
    }
    return (secs * 60 + last) * 1000;
}

QList36<VideoItem> searchResult(cJSON* root) {
    QList36<VideoItem> out;
    cJSON* results = jarr(jobj(root, "data"), "result");
    for (int i = 0; i < jsize(results); ++i) {
        const cJSON* g = jat(results, i);          // result_type 分组
        if (jstr(g, "result_type") != qFromUtf8("video")) continue;
        cJSON* data = jarr(g, "data");
        for (int k = 0; k < jsize(data); ++k) {
            const cJSON* it = jat(data, k);
            VideoItem vi;
            vi.id     = jstr(it, "bvid");
            vi.title  = stripTags(jstr(it, "title"));
            vi.author = jstr(it, "author");
            vi.uid    = QString::number(jint(it, "mid"));
            vi.cover  = picUrl(jstr(it, "pic"));
            // duration:搜索接口是字符串(需解析),列表接口是数字秒(兼容)
            vi.durationMs = jint(it, "duration") * 1000;
            if (vi.durationMs <= 0) {
                vi.durationMs = durationTextMs(jstr(it, "duration"));
            }
            // 搜索接口统计字段:play=播放数, review=评论数(非 stat 子对象)
            vi.stats.viewCount = jint(it, "play");
            vi.stats.replyCount = jint(it, "review");
            vi.extra.insert(qFromUtf8("view"),
                            (long long)vi.stats.viewCount); // UI 优先读 extra["view"]
            if (!vi.id.isEmpty()) out.append(vi);
        }
    }
    return out;
}

Result detailFromView(cJSON* root, VideoItem* viOut, QList36<VideoItem>* partsOut) {
    const cJSON* data = jobj(root, "data");
    if (!data) return Result::fail(Err::Parse, qFromUtf8("bilibili: no data"));
    VideoItem vi;
    vi.id     = jstr(data, "bvid");
    vi.title  = jstr(data, "title");
    vi.author = jstr(jobj(data, "owner"), "name");
    vi.uid    = QString::number(jint(jobj(data, "owner"), "mid"));
    vi.cover  = picUrl(jstr(data, "pic"));
    vi.durationMs = jint(data, "duration") * 1000;
    vi.extra.insert(qFromUtf8("desc"), jstr(data, "desc"));
    vi.extra.insert(qFromUtf8("tname"), jstr(data, "tname"));
    vi.extra.insert(qFromUtf8("pubdate"), (long long)jint(data, "pubdate"));
    vi.publishMs = (qint64)jint(data, "pubdate") * 1000;   // M7
    if (!vi.id.isEmpty())                                  // M7
        vi.webUrl = qFromUtf8("https://www.bilibili.com/video/") + vi.id;
    vi.extra.insert(qFromUtf8("view"),
                    (long long)jint(jobj(data, "stat"), "view"));
    if (const cJSON* st = jobj(data, "stat")) {
        vi.stats.viewCount     = jint(st, "view");
        vi.stats.replyCount    = jint(st, "reply");
        vi.stats.likeCount     = jint(st, "like");
        vi.stats.favoriteCount = jint(st, "favorite");
        vi.stats.coinCount     = jint(st, "coin");
    }
    vi.extra.insert(qFromUtf8("aid"), (long long)jint(data, "aid"));
    vi.extra.insert(qFromUtf8("cid"), QString::number(jint(data, "cid")));

    // 分P:part.id 用 "bvid.cid" 复合形式 → 可直接喂 resolve/resolveQuality
    QList36<VideoItem> parts;
    cJSON* pages = jarr(data, "pages");
    for (int i = 0; i < jsize(pages); ++i) {
        const cJSON* p = jat(pages, i);
        VideoItem part;
        const QString cid = QString::number(jint(p, "cid"));
        part.id = qFromUtf8("%1.%2").arg(vi.id, cid);
        part.title = jstr(p, "part");
        part.durationMs = jint(p, "duration") * 1000;
        part.extra.insert(qFromUtf8("bvid"), vi.id);
        part.extra.insert(qFromUtf8("cid"), cid);
        part.extra.insert(qFromUtf8("page"), (long long)jint(p, "page"));
        parts.append(part);
    }
    if (viOut) *viOut = vi;
    if (partsOut) *partsOut = parts;
    return Result::ok();
}

QList36<VideoItem> popular(cJSON* root, bool* hasMore) {
    const cJSON* data = jobj(root, "data");
    QList36<VideoItem> items;
    cJSON* list = jarr(data, "list");
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        VideoItem vi;
        vi.id     = jstr(it, "bvid");
        vi.title  = stripTags(jstr(it, "title"));
        vi.author = jstr(jobj(it, "owner"), "name");
        vi.uid    = QString::number(jint(jobj(it, "owner"), "mid"));
        vi.cover  = picUrl(jstr(it, "pic"));
        vi.durationMs = jint(it, "duration") * 1000;
        vi.publishMs = (qint64)jint(it, "pubdate") * 1000;          // M7
        if (!vi.id.isEmpty())                                       // M7
            vi.webUrl = qFromUtf8("https://www.bilibili.com/video/") + vi.id;
        vi.extra.insert(qFromUtf8("aid"), (long long)jint(it, "aid"));
        vi.extra.insert(qFromUtf8("tname"), jstr(it, "tname"));
        if (const cJSON* st = jobj(it, "stat")) {
            vi.stats.viewCount  = jint(st, "view");
            vi.stats.replyCount = jint(st, "reply");   // 热门仅 view+reply
        }
        if (!vi.id.isEmpty()) items.append(vi);
    }
    if (hasMore) *hasMore = !jbool(data, "no_more");
    return items;
}

QList36<VideoItem> newlist(cJSON* root, int page, bool* hasMore) {
    const cJSON* data = jobj(root, "data");
    cJSON* arr = jarr(data, "archives");
    QList36<VideoItem> items;
    for (int i = 0; i < jsize(arr); ++i) {
        const cJSON* it = jat(arr, i);
        VideoItem vi;
        vi.id     = jstr(it, "bvid");
        vi.title  = stripTags(jstr(it, "title"));
        vi.author = jstr(jobj(it, "owner"), "name");
        vi.uid    = QString::number(jint(jobj(it, "owner"), "mid"));
        vi.cover  = picUrl(jstr(it, "pic"));
        vi.durationMs = jint(it, "duration") * 1000;
        vi.publishMs = (qint64)jint(it, "pubdate") * 1000;          // M7
        if (!vi.id.isEmpty())                                       // M7
            vi.webUrl = qFromUtf8("https://www.bilibili.com/video/") + vi.id;
        // 播放量:stat.view(部分响应为 0,UI 侧按 0 隐藏,不自造数据)
        vi.extra.insert(qFromUtf8("view"),
                        (long long)jint(jobj(it, "stat"), "view"));
        if (const cJSON* st = jobj(it, "stat")) {
            vi.stats.viewCount  = jint(st, "view");
            vi.stats.replyCount = jint(st, "reply");   // 列表仅 view+reply
        }
        vi.extra.insert(qFromUtf8("aid"), (long long)jint(it, "aid"));
        vi.extra.insert(qFromUtf8("tname"), jstr(it, "tname"));
        if (!vi.id.isEmpty()) items.append(vi);
    }
    if (hasMore) {
        const cJSON* pg = jobj(data, "page");
        const int count = jint(pg, "count", 0);
        const int num   = jint(pg, "num", page);
        const int size  = jint(pg, "size", 20);
        *hasMore = count > 0 && num * size < count;
    }
    return items;
}

QList36<CategoryItem> popularRegions(cJSON* root) {
    const cJSON* data = jobj(root, "data");
    cJSON* list = jarr(data, "list");
    QMap<QString, int> hits;       // tid -> 出现次数
    QMap<QString, QString> names;  // tid -> tname
    QList36<QString> order;          // 首次出现顺序(同频次时保持稳定)
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        const long long tid = jint(it, "tid", 0);
        const QString tname = jstr(it, "tname");
        if (tid <= 0 || tname.isEmpty()) continue;
        const QString k = QString::number(tid) + qFromUtf8("|") + tname;
        if (!hits.contains(k)) order.append(k);
        hits[k] += 1;
        names[k] = tname;
    }
    QList36<CategoryItem> out;
    while (!order.isEmpty()) {     // 频次降序(同频次保持首次出现顺序)
        int best = 0;
        for (int i = 1; i < order.size(); ++i)
            if (hits[order[i]] > hits[order[best]]) best = i;
        const QString k = order[best];
        qstrListRemoveOne(order, k);
        out.append(CategoryItem(k, names[k]));
    }
    return out;
}

QList36<VideoItem> pageList(cJSON* root, const QString& bvid) {
    QList36<VideoItem> parts;
    cJSON* list = jarr(root, "data");
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* p = jat(list, i);
        VideoItem part;
        const QString cid = QString::number(jint(p, "cid"));
        part.id = qFromUtf8("%1.%2").arg(bvid, cid);
        part.title = jstr(p, "part");
        part.durationMs = jint(p, "duration") * 1000;
        part.extra.insert(qFromUtf8("bvid"), bvid);
        part.extra.insert(qFromUtf8("cid"), cid);
        part.extra.insert(qFromUtf8("page"), (long long)jint(p, "page"));
        parts.append(part);
    }
    return parts;
}

// 游客评论:x/v2/reply data.replies[](游客静默仅 3 条,如实返回不伪造)
QList36<CommentItem> parseComments(cJSON* root) {
    QList36<CommentItem> out;
    const cJSON* data = jobj(root, "data");
    cJSON* arr = jarr(data, "replies");
    for (int i = 0; i < jsize(arr); ++i) {
        const cJSON* r = jat(arr, i);
        CommentItem ci;
        ci.id     = QString::number(jint(r, "rpid"));
        ci.author = jstr(jobj(r, "member"), "uname");
        ci.text   = jstr(jobj(r, "content"), "message");
        ci.time   = jint(r, "ctime");
        ci.replyCount = jint(r, "rcount");   /* API 直接给出的该条回复数,不递归 */
        if (ci.replyCount <= 0) ci.replyCount = jint(r, "count");  /* 老字段兜底,仍非递归 */
        if (!ci.id.isEmpty() || !ci.text.isEmpty()) out.append(ci);
    }
    return out;
}

} // namespace parse
} // namespace bilibili
} // namespace qxv
