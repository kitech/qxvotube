#pragma once
#include "common/compat.h"
#include "common/result.h"
#include "common/provider.h"
#include "common/qcjson.h"

#include <QList>

// bilibili 响应解析(纯函数,无网络;供 provider 与离线 fixture 单测共用)。
// 入参统一是"已解析的 cJSON 根"(整个响应体),不校验 code(由调用方 checkApi)。
namespace qxv {
namespace bilibili {
namespace parse {

// /x/web-interface/search/all/v2 → data.result[](result_type=="video")
QList<VideoItem> searchResult(cJSON* root);

// /x/web-interface/view → 主项 + 分P(part.id = "bvid.cid")
Result detailFromView(cJSON* root, VideoItem* vi, QList<VideoItem>* parts);

// /x/web-interface/popular → data.list[];hasMore = !data.no_more
QList<VideoItem> popular(cJSON* root, bool* hasMore);

// /x/web-interface/popular → 条目自带的 (tid,tname) 去重 → 动态分区表
// (按出现频次降序;不含「推荐」项,由 provider 补)
QList<CategoryItem> popularRegions(cJSON* root);

// /x/web-interface/newlist?rid=&pn=&ps= → data.archives[]
// hasMore = data.page.count > 0 && page.num*page.size < page.count
QList<VideoItem> newlist(cJSON* root, int page, bool* hasMore);

// /x/player/pagelist → parts(用于 pages 缺失兜底)
QList<VideoItem> pageList(cJSON* root, const QString& bvid);

} // namespace parse
} // namespace bilibili
} // namespace qxv
