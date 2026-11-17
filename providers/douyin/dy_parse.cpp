#include "parse.h"

namespace qxv {
namespace douyin {
namespace parse {

QList36<VideoItem> hotWords(cJSON* root) {
    QList36<VideoItem> items;
    cJSON* list = jarr(jobj(root, "data"), "word_list");
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        const QString word = jstr(it, "word");
        if (word.isEmpty()) continue;
        VideoItem vi;
        vi.id = qFromUtf8("hot:%1").arg(jstr(it, "group_id"));
        vi.title = word;
        cJSON* covers = jarr(jobj(it, "word_cover"), "url_list");
        const cJSON* c0 = jat(covers, 0);
        if (c0 && cJSON_IsString(c0) && c0->valuestring)
            vi.cover = QString::fromUtf8(c0->valuestring);
        vi.extra.insert(qFromUtf8("hotValue"),
                        (long long)jint(it, "hot_value"));
        vi.extra.insert(qFromUtf8("videoCount"),
                        (long long)jint(it, "video_count"));
        vi.extra.insert(qFromUtf8("discussCount"),
                        (long long)jint(it, "discuss_video_count"));
        vi.extra.insert(qFromUtf8("rank"), i + 1);
        vi.extra.insert(qFromUtf8("kind"), qFromUtf8("hotword"));
        items.append(vi);
    }
    return items;
}

} // namespace parse
} // namespace douyin
} // namespace qxv
