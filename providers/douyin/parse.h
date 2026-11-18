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

// 详情 /aweme/v1/web/aweme/detail/ → aweme_detail
// 取 aweme_id/desc/create_time*1000→publishMs/author.nickname/
// video.duration(ms)→durationMs/cover url_list[0]/webUrl。
// 风控(ArgusSecurityPlugin)/aweme_detail 缺失 → false + err。
bool awemeDetail(cJSON* root, VideoItem* vi, QString* err);

// 从单条 aweme 对象抽取 VideoItem(详情/搜索共用字段口径)。
void fillFromAweme(const cJSON* aw, VideoItem* vi);

// 搜索 /aweme/v1/web/general/search/single/ → data[]
// 只取 type==1 的 aweme_info(视频卡),跳过广告/其他卡片。
QList36<VideoItem> searchList(cJSON* root);

// 取流:video.play_addr 常规兜底档 + bit_rate[] 各档(gear_name→quality,
// 输出按 bitRate 升序,供 resolveQuality 选档)。
QList36<Stream> streamsOf(cJSON* root);

} // namespace parse
} // namespace douyin
} // namespace qxv
