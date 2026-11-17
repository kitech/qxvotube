#pragma once
#include "common/compat.h"
#include "common/provider.h"
#include "common/qcjson.h"

#include <qlist.h>

// 抖音响应解析(纯函数,供 provider 与离线 fixture 单测共用)。
namespace qxv {
namespace douyin {
namespace parse {

// 热榜 /aweme/v1/web/hot/search/list/ → data.word_list[]
// id="hot:<group_id>", title=word, extra: hotValue/videoCount/discussCount/
// rank/kind=hotword/cover
QList36<VideoItem> hotWords(cJSON* root);

} // namespace parse
} // namespace douyin
} // namespace qxv
