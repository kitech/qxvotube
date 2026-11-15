#include "parse.h"

namespace qxv {
namespace hongguo {
namespace parse {

bool extractRouterData(const QByteArray& html, JRoot& out) {
    const QByteArray key("_ROUTER_DATA");
    const int at = html.indexOf(key);
    if (at < 0) return false;
    const char* start = html.constData() + at + key.size();
    const size_t left = static_cast<size_t>(html.size() - at - key.size());
    // 跳过到 '=' 之后第一个 '{'
    int off = 0;
    while (off < (int)left && start[off] != '{') ++off;
    if (off >= (int)left) return false;
    out.p = cJSON_ParseWithLengthOpts(start + off, left - (size_t)off,
                                      nullptr, 0);
    return out.ok();
}

QList<VideoItem> searchList(cJSON* root, int page, bool* hasMore) {
    cJSON* list = jarr(root, "searchList");
    QList<VideoItem> items;
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        const cJSON* vd = jobj(it, "video_data");
        VideoItem vi;
        vi.id    = jstr(vd, "series_id");
        vi.title = jstr(vd, "series_title");
        if (vi.title.isEmpty()) vi.title = jstr(it, "name");
        vi.cover = jstr(vd, "series_cover");
        // 主演取前两名为 author 形态(允许空)
        cJSON* celebs = jarr(vd, "celebrities");
        QString authors;
        for (int k = 0; k < jsize(celebs) && k < 2; ++k) {
            const QString nm = jstr(jat(celebs, k), "name");
            if (!nm.isEmpty())
                authors = authors.isEmpty() ? nm : authors + "/" + nm;
        }
        vi.author = authors;
        QStringList cats;
        cJSON* cls = jarr(vd, "category_list");
        for (int k = 0; k < jsize(cls) && k < 3; ++k)
            cats << jstr(jat(cls, k), "name");
        vi.extra.insert(qFromUtf8("intro"), jstr(vd, "series_intro"));
        vi.extra.insert(qFromUtf8("episodeCnt"),
                        (long long)jint(vd, "episode_cnt"));
        vi.extra.insert(qFromUtf8("payType"), (long long)jint(vd, "pay_type"));
        vi.extra.insert(qFromUtf8("category"), cats.join(qFromUtf8(",")));
        if (!vi.id.isEmpty()) items.append(vi);
    }
    if (hasMore) {
        const long long totalPages = jint(root, "totalPages", 0);
        *hasMore = jbool(root, "hasMore")
                || (totalPages > 0 && page < totalPages);
    }
    return items;
}

QList<VideoItem> homeSections(cJSON* root) {
    const cJSON* pageObj = jobj(jobj(root, "loaderData"), "page");
    cJSON* sections = jarr(pageObj, "homeSections");
    QList<VideoItem> items;
    for (int s = 0; s < jsize(sections); ++s) {
        const cJSON* sec = jat(sections, s);
        cJSON* list = jarr(sec, "video_list");
        for (int i = 0; i < jsize(list); ++i) {
            const cJSON* it = jat(list, i);
            VideoItem vi;
            vi.id     = jstr(it, "series_id");
            vi.title  = jstr(it, "series_title");
            vi.cover  = jstr(it, "series_cover");
            vi.extra.insert(qFromUtf8("intro"), jstr(it, "series_intro"));
            vi.extra.insert(qFromUtf8("episodeCnt"),
                            (long long)jint(it, "episode_cnt"));
            vi.extra.insert(qFromUtf8("payType"),
                            (long long)jint(it, "pay_type"));
            vi.extra.insert(qFromUtf8("rank"), (long long)jint(it, "rank"));
            vi.extra.insert(qFromUtf8("tab"), jstr(sec, "tab_type"));
            if (!vi.id.isEmpty()) items.append(vi);
        }
    }
    return items;
}

QList<VideoItem> rankList(cJSON* root, int page, bool* hasMore) {
    const cJSON* ld = jobj(root, "loaderData");
    const cJSON* pg = nullptr;
    if (ld) {
        // 键名带页面名("rank_hot-drama/page"),遍历取第一个含 content 的
        const cJSON* it = nullptr;
        cJSON_ArrayForEach(it, const_cast<cJSON*>(ld)) {
            if (cJSON_IsObject(it) && jobj(it, "content")) { pg = it; break; }
        }
    }
    QList<VideoItem> items;
    if (!pg) { if (hasMore) *hasMore = false; return items; }
    const cJSON* content = jobj(pg, "content");
    cJSON* list = jarr(content, "rankList");
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        VideoItem vi;
        vi.id = jstr(it, "seriesId");
        if (vi.id.isEmpty()) vi.id = jstr(it, "id");
        vi.title = jstr(it, "title");
        vi.cover = jstr(it, "cover");
        vi.extra.insert(qFromUtf8("rank"), (long long)jint(it, "rank"));
        vi.extra.insert(qFromUtf8("heatText"), jstr(it, "heatText"));
        vi.extra.insert(qFromUtf8("scoreText"), jstr(it, "scoreText"));
        vi.extra.insert(qFromUtf8("href"), jstr(it, "href"));
        vi.extra.insert(qFromUtf8("tab"), qFromUtf8("rank"));
        if (!vi.id.isEmpty()) items.append(vi);
    }
    const cJSON* pagination = jobj(content, "pagination");
    const long long totalPages = jint(pagination, "totalPages", 0);
    if (hasMore)
        *hasMore = totalPages > 0 ? (page < totalPages) : items.size() >= 20;
    return items;
}

Result seriesDetail(cJSON* root, VideoItem* viOut, QList<VideoItem>* partsOut) {
    const cJSON* sd = jobj(jobj(jobj(root, "loaderData"), "detail_page"),
                           "seriesDetail");
    if (!sd) return Result::fail(Err::Parse,
                                 qFromUtf8("hongguo: no seriesDetail"));
    VideoItem vi;
    vi.id    = jstr(sd, "series_id");
    vi.title = jstr(sd, "series_name");
    vi.cover = jstr(sd, "series_cover");
    const long long accessible = jint(sd, "accessible_episode_cnt", -1);
    vi.extra.insert(qFromUtf8("intro"), jstr(sd, "series_intro"));
    vi.extra.insert(qFromUtf8("episodeCnt"), (long long)jint(sd, "episode_cnt"));
    QStringList tags;
    cJSON* tg = jarr(sd, "tags");
    for (int i = 0; i < jsize(tg); ++i) tags << jstrVal(jat(tg, i));
    vi.extra.insert(qFromUtf8("tags"), tags.join(qFromUtf8(",")));
    vi.extra.insert(qFromUtf8("accessibleEpisodeCnt"), accessible);
    vi.extra.insert(qFromUtf8("payType"), (long long)jint(sd, "pay_type"));

    QList<VideoItem> parts;
    cJSON* vids = jarr(sd, "vid_list");
    for (int i = 0; i < jsize(vids); ++i) {
        VideoItem part;
        part.id = jstrVal(jat(vids, i));
        part.title = qFromUtf8("第%1集").arg(i + 1);
        part.extra.insert(qFromUtf8("seriesId"), vi.id);
        // 试看集外的分集打标(UI 可提前灰化;resolve 仍会计费墙)
        part.extra.insert(qFromUtf8("free"), accessible >= 0 && i < accessible);
        parts.append(part);
    }
    if (viOut) *viOut = vi;
    if (partsOut) *partsOut = parts;
    return Result::ok();
}

} // namespace parse
} // namespace hongguo
} // namespace qxv
