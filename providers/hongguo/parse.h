#pragma once
#include "common/compat.h"
#include "common/result.h"
#include "common/provider.h"
#include "common/qcjson.h"

#include <qlist.h>

// 红果官网(通道①)响应解析(纯函数,供 provider 与离线 fixture 单测共用)。
namespace qxv {
namespace hongguo {
namespace parse {

// 从 HTML 里截取 window._ROUTER_DATA = {...}(cJSON_ParseWithLengthOpts 非空终止)
bool extractRouterData(const QByteArray& html, JRoot& out);

// /api/search/result → searchList[](hasMore 来自响应 hasMore/totalPages/page)
QList36<VideoItem> searchList(cJSON* root, int page, bool* hasMore);

// 首页 SSR loaderData.page.homeSections[].video_list[](4 类 tab 汇总)
QList36<VideoItem> homeSections(cJSON* root);

// 首页 SSR homeSections[] → 分类(tab_type/tab_name,实时数据派生,不硬编码)
QList36<CategoryItem> homeTabs(cJSON* root);

// 首页 SSR 按 tab_type 过滤(分类列表;无该 tab → 空)
QList36<VideoItem> homeSection(cJSON* root, const QString& tab);

// /rank/hot-drama?page=N → loaderData["rank_hot-drama/page"].content.rankList[]
QList36<VideoItem> rankList(cJSON* root, int page, bool* hasMore);

// /detail SSR loaderData.detail_page.seriesDetail
Result seriesDetail(cJSON* root, VideoItem* vi, QList36<VideoItem>* parts);

} // namespace parse
} // namespace hongguo
} // namespace qxv
