#include "api.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <string>
#include <vector>

namespace qxv { namespace xhsapi {

namespace {

void appendEscape(std::string& out, const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back((char)c);
        } else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            out += b;
        } else {
            out.push_back((char)c);
        }
    }
}

std::string jstr(const cJSON* o, const char* k) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, k) : 0;
    return (cJSON_IsString(it) && it->valuestring) ? std::string(it->valuestring)
                                                   : std::string();
}
long long jnum(const cJSON* o, const char* k, long long def = 0) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, k) : 0;
    return cJSON_IsNumber(it) ? (long long)it->valuedouble : def;
}
const cJSON* jobj(const cJSON* o, const char* k) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, k) : 0;
    return cJSON_IsObject(it) ? it : 0;
}
const cJSON* jarr(const cJSON* o, const char* k) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, k) : 0;
    return cJSON_IsArray(it) ? it : 0;
}
bool jbool(const cJSON* o, const char* k) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, k) : 0;
    if (!it) return false;
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) != 0;
    if (cJSON_IsNumber(it)) return it->valuedouble != 0;
    return false;
}

// note_card / user_posted 条目 → NoteItem
// outer 提供 id/xsec_token/xsec_source(search 的 item 层);
// user_posted 的条目本身即 note_card 形状(outer==card)。
NoteItem noteFromCard(const cJSON* card, const cJSON* outer) {
    NoteItem n;
    n.id = jstr(outer, "id");
    if (n.id.empty()) n.id = jstr(card, "note_id");
    if (n.id.empty()) n.id = jstr(card, "id");
    n.title = jstr(card, "display_title");
    if (n.title.empty()) n.title = jstr(card, "title");
    n.desc = jstr(card, "desc");
    n.type = jstr(card, "type");
    const cJSON* user = jobj(card, "user");
    n.author = jstr(user, "nickname");
    if (n.author.empty()) n.author = jstr(user, "nick_name");
    n.authorId = jstr(user, "user_id");
    const cJSON* cover = jobj(card, "cover");
    n.cover = jstr(cover, "url_default");
    if (n.cover.empty()) n.cover = jstr(cover, "url_pre");
    const cJSON* inter = jobj(card, "interact_info");
    const cJSON* liked = inter
        ? cJSON_GetObjectItemCaseSensitive(inter, "liked_count") : 0;
    if (cJSON_IsString(liked)) n.likedCount = atoll(liked->valuestring);
    else if (cJSON_IsNumber(liked)) n.likedCount = (long long)liked->valuedouble;
    n.xsecToken = jstr(outer, "xsec_token");
    n.xsecSource = jstr(outer, "xsec_source");
    if (n.xsecToken.empty()) n.xsecToken = jstr(card, "xsec_token");
    return n;
}

cJSON* parseRoot(const std::string& body) {
    if (body.empty()) return 0;
    return cJSON_ParseWithLength(body.data(), body.size());
}

} // namespace

std::string queryEncode(
    const std::vector<std::pair<std::string, std::string> >& kv) {
    std::string out;
    for (size_t i = 0; i < kv.size(); ++i) {
        if (i) out.push_back('&');
        out += kv[i].first;
        out.push_back('=');
        const std::string& v = kv[i].second;
        for (size_t k = 0; k < v.size(); ++k) {
            const unsigned char c = (unsigned char)v[k];
            const bool unreserved = (c >= 'A' && c <= 'Z')
                                 || (c >= 'a' && c <= 'z')
                                 || (c >= '0' && c <= '9')
                                 || c == '-' || c == '_' || c == '.' || c == '~'
                                 || c == ',';   // xhshow quote(value, safe=",")
            if (unreserved) { out.push_back((char)c); continue; }
            char b[8];
            snprintf(b, sizeof(b), "%%%02X", c);
            out += b;
        }
    }
    return out;
}

std::string buildSearchBody(const std::string& keyword, int page,
                            int pageSize, const std::string& searchIdV,
                            const std::string& sort, int noteType) {
    char buf[160];
    std::string out = "{\"keyword\":\"";
    appendEscape(out, keyword);
    snprintf(buf, sizeof(buf),
             "\",\"page\":%d,\"page_size\":%d,\"search_id\":\"", page, pageSize);
    out += buf;
    appendEscape(out, searchIdV);
    snprintf(buf, sizeof(buf), "\",\"sort\":\"%s\",\"note_type\":%d}",
             sort.c_str(), noteType);
    out += buf;
    return out;
}

std::string buildFeedBody(const std::string& noteId,
                          const std::string& xsecSource,
                          const std::string& xsecToken) {
    std::string out = "{\"source_note_id\":\"";
    appendEscape(out, noteId);
    out += "\",\"image_formats\":[\"jpg\",\"webp\",\"avif\"],"
           "\"extra\":{\"need_body_topic\":1},\"xsec_source\":\"";
    appendEscape(out, xsecSource.empty() ? std::string("pc_feed") : xsecSource);
    out += "\",\"xsec_token\":\"";
    appendEscape(out, xsecToken);
    out += "\"}";
    return out;
}

std::string searchId() {
    const uint64_t ms = (uint64_t)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t v = (ms << 64) + (uint64_t)(rand() % 2147483646);
    if (!v) return "0";
    std::string out;
    while (v) {
        const int d = (int)(v % 36);
        out.insert(out.begin(), (char)(d < 10 ? '0' + d : 'A' + d - 10));
        v /= 36;
    }
    return out;
}

PagedNotes parseSearch(const std::string& body) {
    PagedNotes out;
    cJSON* root = parseRoot(body);
    if (!root) return out;
    const cJSON* data = jobj(root, "data");
    const cJSON* items = jarr(data, "items");
    out.hasMore = jbool(data, "has_more");
    for (const cJSON* it = items ? items->child : 0; it; it = it->next) {
        const std::string mt = jstr(it, "model_type");
        if (mt == "rec_query" || mt == "hot_query") continue;   // 聚合词条
        const cJSON* card = jobj(it, "note_card");
        if (!card) continue;
        out.items.push_back(noteFromCard(card, it));
    }
    cJSON_Delete(root);
    return out;
}

PagedNotes parseUserPosted(const std::string& body) {
    PagedNotes out;
    cJSON* root = parseRoot(body);
    if (!root) return out;
    const cJSON* data = jobj(root, "data");
    out.cursor = jstr(data, "cursor");
    out.hasMore = jbool(data, "has_more");
    const cJSON* notes = jarr(data, "notes");
    for (const cJSON* it = notes ? notes->child : 0; it; it = it->next) {
        const cJSON* card = jobj(it, "note_card");
        if (!card) card = it;   // user_posted 条目本身即 note_card 形状
        out.items.push_back(noteFromCard(card, it));
    }
    cJSON_Delete(root);
    return out;
}

NoteDetail parseFeed(const std::string& body) {
    NoteDetail d;
    cJSON* root = parseRoot(body);
    if (!root) return d;
    const cJSON* data = jobj(root, "data");
    const cJSON* items = jarr(data, "items");
    const cJSON* first = items ? items->child : 0;
    const cJSON* card = jobj(first, "note_card");
    if (!card) { cJSON_Delete(root); return d; }
    d.note = noteFromCard(card, first);
    const cJSON* video = jobj(card, "video");
    const cJSON* media = jobj(video, "media");
    const cJSON* stream = jobj(media, "stream");
    // h264 优先,h265 兜底;取首个 master_url
    static const char* kCodecs[2] = {"h264", "h265"};
    for (int c = 0; c < 2 && d.streamUrl.empty(); ++c) {
        const cJSON* arr = jarr(stream, kCodecs[c]);
        for (const cJSON* s = arr ? arr->child : 0; s; s = s->next) {
            const std::string u = jstr(s, "master_url");
            if (!u.empty()) {
                d.streamUrl = u;
                d.width = (int)jnum(s, "width", 0);
                d.height = (int)jnum(s, "height", 0);
                const cJSON* bk = jarr(s, "backup_urls");
                for (const cJSON* b = bk ? bk->child : 0; b; b = b->next)
                    if (cJSON_IsString(b) && b->valuestring)
                        d.backupUrls.push_back(b->valuestring);
                break;
            }
        }
    }
    d.durationMs = jnum(jobj(video, "capa"), "duration", 0);
    cJSON_Delete(root);
    return d;
}

CommentPageLite parseComments(const std::string& body) {
    CommentPageLite out;
    cJSON* root = parseRoot(body);
    if (!root) return out;
    const cJSON* data = jobj(root, "data");
    out.cursor = jstr(data, "cursor");
    out.hasMore = jbool(data, "has_more");
    const cJSON* arr = jarr(data, "comments");
    for (const cJSON* c = arr ? arr->child : 0; c; c = c->next) {
        CommentItemLite ci;
        ci.id = jstr(c, "id");
        ci.content = jstr(c, "content");
        const cJSON* user = jobj(c, "user_info");
        ci.author = jstr(user, "nickname");
        const cJSON* like = cJSON_GetObjectItemCaseSensitive(c, "like_count");
        if (cJSON_IsString(like)) ci.likeCount = atoll(like->valuestring);
        else if (cJSON_IsNumber(like)) ci.likeCount = (long long)like->valuedouble;
        ci.createTime = jnum(c, "create_time", 0);
        out.items.push_back(ci);
    }
    cJSON_Delete(root);
    return out;
}

bool parseSelfOk(const std::string& body) {
    cJSON* root = parseRoot(body);
    if (!root) return false;
    const cJSON* data = jobj(root, "data");
    const cJSON* result = jobj(data, "result");
    bool ok = jbool(result, "success");
    if (!ok) ok = jnum(root, "code", -1) == 0
                  && cJSON_GetObjectItemCaseSensitive(root, "code") != 0;
    cJSON_Delete(root);
    return ok;
}

}} // namespace qxv::xhsapi
