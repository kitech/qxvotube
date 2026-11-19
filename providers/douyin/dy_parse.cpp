#include "parse.h"

#include <vector>

namespace qxv {
namespace douyin {
namespace parse {

// URL path 段百分号编码(保留 unreserved;中文按 UTF-8 逐字节 %XX)。
// douyin 搜索页路径需要它;不做 form 编码(' '→%20 而非 '+')。
static QString urlPathEncode(const QString& s) {
    static const char* kHex = "0123456789ABCDEF";
    const QByteArray u = qToUtf8(s);
    const char* p = u.data();
    QString out;
    for (int i = 0; p[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') {
            out += QChar(c);
        } else {
            out += QChar('%');
            out += QChar(kHex[c >> 4]);
            out += QChar(kHex[c & 15]);
        }
    }
    return out;
}

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
        // 热词没有视频详情(匿名搜索已关闭),但给出原站搜索页:
        // UI 右键「在浏览器打开」可直接看该话题;detail/resolve 显式报错。
        vi.webUrl = qFromUtf8("https://www.douyin.com/search/")
                    + urlPathEncode(word);
        items.append(vi);
    }
    return items;
}

// 从单条 aweme 对象抽取 VideoItem(详情/搜索共用)
void fillFromAweme(const cJSON* aw, VideoItem* vi) {
    vi->id = jstr(aw, "aweme_id");
    vi->title = jstr(aw, "desc");
    vi->publishMs = jint(aw, "create_time") * 1000;   // 秒 → ms
    vi->author = jstr(jobj(aw, "author"), "nickname");
    cJSON* video = jobj(aw, "video");
    if (video) {
        vi->durationMs = jint(video, "duration");     // 已是 ms
        cJSON* covers = jarr(jobj(video, "cover"), "url_list");
        const cJSON* c0 = jat(covers, 0);
        if (c0 && cJSON_IsString(c0) && c0->valuestring)
            vi->cover = QString::fromUtf8(c0->valuestring);
    }
    vi->webUrl = qFromUtf8("https://www.douyin.com/video/%1").arg(vi->id);
    // statistics:网页端详情/搜索均带;play_count 站外常为 0,UI 按 0 隐藏
    const cJSON* st = jobj(aw, "statistics");
    if (st) {
        vi->stats.viewCount     = jint(st, "play_count");
        vi->stats.replyCount    = jint(st, "comment_count");
        vi->stats.likeCount     = jint(st, "digg_count");
        vi->stats.favoriteCount = jint(st, "collect_count");
    }
}

// 详情解析:root.aweme_detail;缺失 → 风控/不存在,带 filter_detail.msg
bool awemeDetail(cJSON* root, VideoItem* vi, QString* err) {
    cJSON* aw = jobj(root, "aweme_detail");
    if (!aw) {
        const QString filterMsg = jstr(jobj(root, "filter_detail"), "detail_msg");
        *err = filterMsg.isEmpty()
            ? qFromUtf8("douyin: aweme_detail 缺失(风控或视频不存在)")
            : qFromUtf8("douyin: %1").arg(filterMsg);
        return false;
    }
    fillFromAweme(aw, vi);
    if (vi->id.isEmpty()) { *err = qFromUtf8("douyin: aweme_id 为空"); return false; }
    return true;
}

// 搜索:root.data[] 数组,只收 type==1(aweme_info 视频卡),跳过广告/其他卡片
QList36<VideoItem> searchList(cJSON* root) {
    QList36<VideoItem> items;
    cJSON* arr = jarr(root, "data");
    for (int i = 0; i < jsize(arr); ++i) {
        const cJSON* it = jat(arr, i);
        if (jint(it, "type") != 1) continue;          // 1=aweme 视频卡
        cJSON* aw = jobj(it, "aweme_info");
        if (!aw) continue;
        VideoItem vi;
        fillFromAweme(aw, &vi);
        if (vi.id.isEmpty()) continue;
        items.append(vi);
    }
    return items;
}

// gear_name → 档位标签:"normal_1080_0"→"1080P"、"adapt_low_540_1"→"540P"、
// "gear_360p_500k"→"360P";解析不出 → 原值(不丢档位)。
static QString gearName(const QString& gear) {
    for (int i = 0; i < (int)gear.length(); ++i) {
        if (gear[i] < QChar('0') || gear[i] > QChar('9')) continue;
        QString num;
        int j = i;
        for (; j < (int)gear.length(); ++j) {
            const QChar c = gear[j];
            if (c < QChar('0') || c > QChar('9')) break;
            num += c;
        }
        // 3~4 位数字段 + 前后为分隔符(避免把 640k 码率当分辨率)
        if ((num.length() == 3 || num.length() == 4)
            && (i == 0 || gear[i-1] == QChar('_') || gear[i-1] == QChar('-'))
            && (j >= (int)gear.length() || gear[j] == QChar('_')
                || gear[j] == QChar('p') || gear[j] == QChar('P')))
            return num + qFromUtf8("P");
        i = j;
    }
    return gear;
}

// 取流:video.play_addr 兜底 + bit_rate[] 档位(gear_name→quality),bitRate 升序
// 注:Qt3 QValueList::operator[] 为值语义(不可用于排序交换),内部走 std::vector。
QList36<Stream> streamsOf(cJSON* root) {
    std::vector<Stream> all;
    cJSON* video = jobj(jobj(root, "aweme_detail"), "video");
    if (!video) return QList36<Stream>();
    cJSON* brs = jarr(video, "bit_rate");
    for (int i = 0; i < jsize(brs); ++i) {
        const cJSON* br = jat(brs, i);
        cJSON* pa = jobj(br, "play_addr");
        cJSON* urls = jarr(pa, "url_list");
        const cJSON* u0 = jat(urls, 0);
        if (!u0 || !cJSON_IsString(u0) || !u0->valuestring) continue;
        Stream s;
        s.url = QString::fromUtf8(u0->valuestring);
        s.quality = gearName(jstr(br, "gear_name"));
        s.kind = qFromUtf8("mp4");
        s.extra.insert(qFromUtf8("bitRate"), jint(br, "bit_rate"));
        s.extra.insert(qFromUtf8("gear"), jstr(br, "gear_name"));
        all.push_back(s);
    }
    if (all.empty()) {   // 兜底:video.play_addr
        cJSON* urls = jarr(jobj(video, "play_addr"), "url_list");
        const cJSON* u0 = jat(urls, 0);
        if (u0 && cJSON_IsString(u0) && u0->valuestring) {
            Stream s;
            s.url = QString::fromUtf8(u0->valuestring);
            s.quality = jstr(video, "ratio");
            s.kind = qFromUtf8("mp4");
            all.push_back(s);
        }
    }
    // bitRate 升序(档位 ≤20,插入排序足够;同码率保持接口原序)
    for (size_t i = 1; i < all.size(); ++i) {
        Stream v = all[i];
        const long long vb = v.extra[qFromUtf8("bitRate")].toLongLong();
        size_t j = i;
        while (j > 0 && all[j-1].extra[qFromUtf8("bitRate")].toLongLong() > vb) {
            all[j] = all[j-1];
            --j;
        }
        all[j] = v;
    }
    QList36<Stream> out;
    for (size_t i = 0; i < all.size(); ++i) out.append(all[i]);
    return out;
}

} // namespace parse
} // namespace douyin
} // namespace qxv
