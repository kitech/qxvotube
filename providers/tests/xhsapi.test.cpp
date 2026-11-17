// 小红书 web API 解析器离线验收(合成 fixture,按 schema 手造):
//   请求构造(search body / feed body / query 编码 / search_id)
//   响应解析(search 跳过 hot_query+rec_query / feed 取 h264 / comments / user_posted)
//   schema 边界(缺 note_card、空 items、字符串数字混用)→ 降级不崩
//
// 依据:MediaCrawler media_platform/xhs/{client,core}.py(端点/字段)+
//       xhshow client.py(query 编码 safe="," 等)。纯 C++,零 Qt。
// 用法: xhsapi [fixture 目录]   默认 tests/fixtures/xhs_api
#include "xiaohongshu/api.h"

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace qxv::xhsapi;

static int g_fail = 0;
static void check(bool ok, const char* what, const std::string& got = "") {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, got.c_str()); }
}

static std::string slurp(const std::string& p) {
    std::ifstream f(p.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    std::string dir = "tests/fixtures/xhs_api";
    for (int i = 1; i < argc; ++i) dir = argv[i];

    // ── 请求构造 ─────────────────────────────────────────────
    {
        std::vector<std::pair<std::string, std::string> > kv;
        kv.push_back(std::make_pair(std::string("note_id"), std::string("abc")));
        kv.push_back(std::make_pair(std::string("cursor"), std::string("")));
        kv.push_back(std::make_pair(std::string("image_formats"),
                                    std::string("jpg,webp,avif")));
        const std::string q = queryEncode(kv);
        check(q == "note_id=abc&cursor=&image_formats=jpg,webp,avif",
              "queryEncode(逗号安全)", q);

        const std::string b =
            buildSearchBody("猫 \"狗\"", 1, 20, "SID", "general", 0);
        check(b.find("\"keyword\":\"猫 \\\"狗\\\"\"") != std::string::npos,
              "buildSearchBody 转义");
        check(b.find("\"page\":1,\"page_size\":20") != std::string::npos,
              "buildSearchBody 分页");
        check(b.find("\"note_type\":0") != std::string::npos,
              "buildSearchBody note_type");

        const std::string fb = buildFeedBody("N1", "", "T1");
        check(fb.find("\"xsec_source\":\"pc_feed\"") != std::string::npos,
              "buildFeedBody 默认 xsec_source");
        check(fb.find("\"image_formats\":[\"jpg\",\"webp\",\"avif\"]")
                  != std::string::npos, "buildFeedBody image_formats");
        check(fb.find("\"extra\":{\"need_body_topic\":1}") != std::string::npos,
              "buildFeedBody extra");
        check(searchId().size() > 5, "search_id 非空");
    }

    // ── 搜索解析 ─────────────────────────────────────────────
    {
        const PagedNotes p = parseSearch(slurp(dir + "/search.json"));
        check(p.items.size() == 2, "search 跳过 hot/rec_query",
              std::to_string(p.items.size()));
        check(p.hasMore, "search has_more");
        check(p.items[0].id == "66fad51c000000001b0224b8", "search id");
        check(p.items[0].title == "示例视频笔记", "search title");
        check(p.items[0].author == "作者A", "search author");
        check(p.items[0].cover.find("xhscdn") != std::string::npos,
              "search cover");
        check(p.items[0].likedCount == 1234, "search liked_count 字符串→数字");
        check(p.items[0].isVideo(), "search type video");
        check(!p.items[1].isVideo() && p.items[1].likedCount == 77,
              "search 图文 + 数字 liked_count");
        check(p.items[0].xsecToken == "AB3rTOKEN", "search xsec_token");
    }

    // ── 详情(视频流 h264 优先)───────────────────────────────
    {
        const NoteDetail d = parseFeed(slurp(dir + "/feed.json"));
        check(d.note.id == "66fad51c000000001b0224b8", "feed id");
        check(d.streamUrl == "https://sns-video-bd.xhscdn.com/stream/1",
              "feed h264 master_url", d.streamUrl);
        check(d.backupUrls.size() == 1, "feed backup_urls");
        check(d.durationMs == 15234, "feed duration");
        check(d.width == 1920 && d.height == 1080, "feed 分辨率");
    }

    // ── 评论 ────────────────────────────────────────────────
    {
        const CommentPageLite c = parseComments(slurp(dir + "/comments.json"));
        check(c.items.size() == 2 && c.hasMore && c.cursor == "CUR2",
              "comments 基本");
        check(c.items[0].author == "用户甲" && c.items[0].likeCount == 12,
              "comments 作者/点赞(字符串)");
        check(c.items[1].likeCount == 3, "comments 点赞(数字)");
    }

    // ── 用户页 ──────────────────────────────────────────────
    {
        const PagedNotes u = parseUserPosted(slurp(dir + "/user_posted.json"));
        check(u.items.size() == 1 && u.items[0].title == "用户页视频",
              "user_posted");
        check(!u.hasMore && u.cursor == "CUR9", "user_posted 分页");
    }

    // ── 登录/错误 ────────────────────────────────────────────
    check(parseSelfOk(slurp(dir + "/selfinfo.json")), "selfinfo success");
    {
        const PagedNotes e = parseSearch(slurp(dir + "/error_101.json"));
        check(e.items.empty(), "错误体 → 空结果不崩");
        check(!parseSelfOk(slurp(dir + "/error_101.json")), "selfinfo -101 判否");
        check(parseSearch("not json").items.empty(), "非 JSON → 空结果");
        check(parseFeed("{}").streamUrl.empty(), "空对象 → 无流");
        check(parseComments("").items.empty(), "空 body → 空评论");
    }

    std::printf("xhsapi: %s\n", g_fail ? "FAILED" : "all green");
    return g_fail ? 1 : 0;
}
