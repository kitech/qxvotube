#include "parse.h"

// 口径:docs/providers/xiaohongshu-research.md §2

namespace qxv {
namespace xiaohongshu {
namespace parse {

namespace {

QString coverUrl(const cJSON* cover) {
    if (!cover) return QString();
    QString u = jstr(cover, "urlDefault");
    if (u.isEmpty()) u = jstr(cover, "urlPre");
    if (u.isEmpty()) u = jstr(cover, "url");
    if (u.isEmpty()) {
        cJSON* info = jarr(cover, "infoList");
        u = jstr(jat(info, 0), "url");
    }
    return u;
}

// 从 noteCard(首页/用户页通用)构造 VideoItem
VideoItem fromNoteCard(const cJSON* card, const QString& noteId,
                       const QString& tokenIn) {
    VideoItem vi;
    vi.id = noteId;
    if (vi.id.isEmpty()) vi.id = jstr(card, "noteId");
    QString token = tokenIn;
    if (token.isEmpty()) token = jstr(card, "xsecToken");
    vi.title = jstr(card, "displayTitle");
    const cJSON* user = jobj(card, "user");
    vi.author = jstr(user, "nickname");
    if (vi.author.isEmpty()) vi.author = jstr(user, "nickName");
    vi.uid = jstr(user, "userId");
    vi.cover = coverUrl(jobj(card, "cover"));
    const cJSON* inter = jobj(card, "interactInfo");
    vi.extra.insert(qFromUtf8("type"), jstr(card, "type"));
    if (!token.isEmpty())
        vi.extra.insert(qFromUtf8("xsecToken"), token);
    if (!jstr(inter, "likedCount").isEmpty())
        vi.extra.insert(qFromUtf8("likedCount"), jstr(inter, "likedCount"));
    vi.extra.insert(qFromUtf8("noteId"), noteId);
    return vi;
}

} // namespace

QList<VideoItem> feedNotes(cJSON* root) {
    QList<VideoItem> out;
    const cJSON* feed = jobj(root, "feed");
    cJSON* feeds = jarr(feed, "feeds");
    for (int i = 0; i < jsize(feeds); ++i) {
        const cJSON* f = jat(feeds, i);
        const QString id = jstr(f, "id");
        if (id.isEmpty()) continue;
        const cJSON* card = jobj(f, "noteCard");
        if (!card) continue;
        out.append(fromNoteCard(card, id, jstr(f, "xsecToken")));
    }
    return out;
}

QList<VideoItem> userNotes(cJSON* root) {
    QList<VideoItem> out;
    const cJSON* user = jobj(root, "user");
    cJSON* tabs = jarr(user, "notes");   // [[{id,noteCard,xsecToken}...], ...]
    for (int t = 0; t < jsize(tabs); ++t) {
        const cJSON* tab = jat(tabs, t);
        if (!cJSON_IsArray(tab)) continue;
        for (int i = 0; i < jsize(tab); ++i) {
            const cJSON* it = jat(tab, i);
            const cJSON* card = jobj(it, "noteCard");
            if (!card) continue;
            // 实测:用户页 SSR 的 id/noteCard.noteId 常为空(需登录才带 id),
            // 此时只能给出元数据 + xsecToken → 标 playable=false
            const QString id = jstr(it, "id");
            VideoItem vi = fromNoteCard(card, id, jstr(it, "xsecToken"));
            if (vi.id.isEmpty()) {
                vi.extra.insert(qFromUtf8("playable"), false);
                vi.extra.insert(qFromUtf8("needLoginForId"), true);
            }
            out.append(vi);
        }
    }
    return out;
}

Result noteDetail(cJSON* root, const QString& noteId, VideoItem* viOut,
                  QList<Stream>* streamsOut) {
    const cJSON* noteRoot = jobj(root, "note");
    const cJSON* map = jobj(noteRoot, "noteDetailMap");
    if (!map) return Result::fail(Err::Parse, qFromUtf8("xhs: no noteDetailMap"));

    const cJSON* entry = nullptr;
    if (!noteId.isEmpty()) entry = jobj(map, noteId.toUtf8().constData());
    if (!entry) {
        // 分享链接里 id 与返回 key 可能大小写/后缀不同:取第一个
        const cJSON* it = nullptr;
        cJSON_ArrayForEach(it, const_cast<cJSON*>(map)) {
            if (cJSON_IsObject(it)) { entry = it; break; }
        }
    }
    if (!entry) return Result::fail(Err::Parse, qFromUtf8("xhs: no note entry"));
    const cJSON* note = jobj(entry, "note");
    if (!note) return Result::fail(Err::Parse, qFromUtf8("xhs: no note"));

    VideoItem vi;
    vi.id = jstr(note, "noteId");
    if (vi.id.isEmpty()) vi.id = noteId;
    vi.title = jstr(note, "title");
    const cJSON* user = jobj(note, "user");
    vi.author = jstr(user, "nickname");
    if (vi.author.isEmpty()) vi.author = jstr(user, "nickName");
    vi.uid = jstr(user, "userId");
    vi.extra.insert(qFromUtf8("desc"), jstr(note, "desc"));
    vi.extra.insert(qFromUtf8("type"), jstr(note, "type"));
    const cJSON* inter = jobj(note, "interactInfo");
    vi.extra.insert(qFromUtf8("likedCount"), jstr(inter, "likedCount"));
    vi.extra.insert(qFromUtf8("collectedCount"), jstr(inter, "collectedCount"));
    vi.extra.insert(qFromUtf8("commentCount"), jstr(inter, "commentCount"));

    cJSON* images = jarr(note, "imageList");
    if (jsize(images) > 0) vi.cover = coverUrl(jat(images, 0));

    QList<Stream> streams;
    const cJSON* video = jobj(note, "video");
    if (video) {
        const cJSON* streamRoot = jobj(jobj(video, "media"), "stream");
        // 编码优先级:h264(兼容最好) > h265 > av1
        const char* codecs[] = { "h264", "h265", "av1" };
        for (const char* codec : codecs) {
            cJSON* list = streamRoot ? jarr(streamRoot, codec) : nullptr;
            for (int i = 0; i < jsize(list); ++i) {
                const cJSON* s = jat(list, i);
                QString url = jstr(s, "masterUrl");
                if (url.isEmpty()) continue;
                if (url.startsWith(qFromUtf8("http://")))
                    url.replace(0, 7, qFromUtf8("https://"));
                Stream st;
                st.url = url;
                st.quality = qFromUtf8("%1x%2")
                                 .arg(static_cast<int>(jint(s, "width")))
                                 .arg(static_cast<int>(jint(s, "height")));
                st.kind = qFromUtf8("mp4");
                st.extra.insert(qFromUtf8("codec"), qFromUtf8(codec));
                st.extra.insert(qFromUtf8("size"),
                                (long long)jint(s, "size"));
                st.extra.insert(qFromUtf8("fps"), (long long)jint(s, "fps"));
                st.extra.insert(qFromUtf8("bitrate"),
                                (long long)jint(s, "avgBitrate"));
                st.extra.insert(qFromUtf8("audioCodec"), jstr(s, "audioCodec"));
                cJSON* bk = jarr(s, "backupUrls");
                QStringList backups;
                for (int k = 0; k < jsize(bk); ++k) {
                    QString b = jstrVal(jat(bk, k));
                    if (b.startsWith(qFromUtf8("http://")))
                        b.replace(0, 7, qFromUtf8("https://"));
                    if (!b.isEmpty()) backups << b;
                }
                if (!backups.isEmpty())
                    st.extra.insert(qFromUtf8("backupUrls"),
                                    backups.join(qFromUtf8("\n")));
                // 官方客户端需要 Referer 防盗链(部分 CDN)
                st.extra.insert(qFromUtf8("Referer"),
                                qFromUtf8("https://www.xiaohongshu.com/"));
                streams.append(st);
            }
            if (!streams.isEmpty()) break;   // 只用首个可用编码族
        }
    }
    if (streams.isEmpty()) {
        if (viOut) *viOut = vi;
        if (streamsOut) streamsOut->clear();
        return Result::fail(Err::Unsupported,
                            qFromUtf8("xhs: 图文笔记无视频流"));
    }
    if (viOut) *viOut = vi;
    if (streamsOut) *streamsOut = streams;
    return Result::ok();
}

bool parseShareUrl(const QString& url, QString* noteId, QString* xsecToken) {
    // 形如 .../explore/<24hex>?xsec_token=xxx&xsec_source=pc_share
    const int ex = url.indexOf(qFromUtf8("/explore/"));
    if (ex >= 0) {
        const int start = ex + 9;
        int end = url.indexOf('?', start);
        if (end < 0) end = url.indexOf('/', start);
        if (end < 0) end = url.size();
        const QString id = url.mid(start, end - start);
        if (!id.isEmpty() && noteId) *noteId = id;
    }
    const int t = url.indexOf(qFromUtf8("xsec_token="));
    if (t >= 0) {
        const int start = t + 11;
        int end = url.indexOf('&', start);
        if (end < 0) end = url.size();
        if (xsecToken) *xsecToken = url.mid(start, end - start);
    }
    return (noteId && !noteId->isEmpty());
}

} // namespace parse
} // namespace xiaohongshu
} // namespace qxv
