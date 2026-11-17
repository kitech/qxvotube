#pragma once
// 小红书登录态 web API 的请求构造与响应解析(纯 C++,零 Qt)。
//
// 依据(源码级):MediaCrawler media_platform/xhs/{client,core}.py +
// xhshow client.py 头口径。**未经真实服务端样本验证**:游客一律 -101,
// 无 cookie 环境无法取得响应;解析器对未知结构一律降级(空列表/空串),
// 并用 tests/fixtures/xhs_api/*.json(按 schema 合成)离线单测。
//
// 端点:
//   POST /api/sns/web/v1/search/notes   {keyword,page,page_size,search_id,sort,note_type}
//   POST /api/sns/web/v1/feed           {source_note_id,image_formats,extra,xsec_source,xsec_token}
//   GET  /api/sns/web/v2/comment/page   note_id,cursor,top_comment_id,image_formats,xsec_token
//   GET  /api/sns/web/v1/user_posted    num,cursor,user_id,image_formats,xsec_token,xsec_source
//   GET  /api/sns/web/v1/user/selfinfo

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

namespace qxv { namespace xhsapi {

struct NoteItem {
    std::string id, title, desc, cover, author, authorId, type;  // type: video/normal
    long long likedCount = 0;
    std::string xsecToken, xsecSource;
    bool isVideo() const { return type == "video"; }
};

struct PagedNotes {
    std::vector<NoteItem> items;
    std::string cursor;
    bool hasMore = false;
};

struct NoteDetail {
    NoteItem note;
    std::string streamUrl;    // video.media.stream.h264[].master_url 首个
    std::vector<std::string> backupUrls;
    int width = 0, height = 0;   // 首选流分辨率(quality 归一化用)
    long long durationMs = 0;
};

struct CommentItemLite {
    std::string id, content, author, timeText;
    long long likeCount = 0;
    long long createTime = 0;   // 秒
};

struct CommentPageLite {
    std::vector<CommentItemLite> items;
    std::string cursor;
    bool hasMore = false;
};

// ── 请求 ─────────────────────────────────────────────────────
// xhshow GET content 口径:quote(value, safe=",");逗号/未保留字符不转义
std::string queryEncode(
    const std::vector<std::pair<std::string, std::string> >& kv);
// {"keyword":…,"page":…,"page_size":…,"search_id":…,"sort":…,"note_type":…}
std::string buildSearchBody(const std::string& keyword, int page,
                            int pageSize, const std::string& searchId,
                            const std::string& sort, int noteType);
// {"source_note_id":…,"image_formats":["jpg","webp","avif"],
//  "extra":{"need_body_topic":1},"xsec_source":…,"xsec_token":…}
std::string buildFeedBody(const std::string& noteId,
                          const std::string& xsecSource,
                          const std::string& xsecToken);
// base36((nowMs << 64) + rand)(MediaCrawler get_search_id 口径)
std::string searchId();

// ── 响应(入参为完整 body,内部取 .data;未知结构 → 空结果,不抛)────
PagedNotes parseSearch(const std::string& body);
PagedNotes parseUserPosted(const std::string& body);
NoteDetail parseFeed(const std::string& body);
CommentPageLite parseComments(const std::string& body);
bool parseSelfOk(const std::string& body);   // data.result.success

}} // namespace qxv::xhsapi
