#pragma once
#include "common/compat.h"
#include "common/result.h"
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

// /x/player/pagelist → parts(用于 pages 缺失兜底)
QList<VideoItem> pageList(cJSON* root, const QString& bvid);

} // namespace parse
} // namespace bilibili
} // namespace qxv
