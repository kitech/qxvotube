#pragma once
#include "common/compat.h"
#include "common/provider.h"
#include "common/qcjson.h"

#include <qlist.h>
#include <qstring.h>

// YouTube InnerTube 响应解析(纯函数,供 provider 与离线 fixture 单测共用)。
namespace qxv {
namespace youtube {
namespace parse {

// search 响应:videoRenderer(WEB) + compactVideoRenderer(ANDROID/IOS)
QList36<VideoItem> searchItems(cJSON* root);

// guide 响应(youtubei/v1/guide):左侧导航项 → 分类 chips。
// 只取 browseEndpoint.browseId 以 "UC" 开头的真实主题(Music/Live/Gaming),
// 滤掉 Home/Subscriptions/You/History/Memberships 等 FE* 导航项
// (Movies=FEstorefront 在本出口 IP 为空壳,同样被滤掉)。
// key = browseId,或 browseId|params(带 params 的主题)。
QList36<CategoryItem> guideItems(cJSON* root);

// browse 响应(分类页):videoRenderer(Live) / videoCardRenderer(Gaming) /
// lockupViewModel(Music,仅 LOCKUP_CONTENT_TYPE_VIDEO;playlist 条目跳过)
QList36<VideoItem> browseItems(cJSON* root);

// 续页游标:continuationCommand.token / nextContinuationData.continuation /
// reloadContinuationData.continuation(任一,空=没有下一页)
QString continuation(cJSON* root);

// playlist browse:按 watchEndpoint{videoId,playlistId} 收集(去重,上限 cap)
QList36<QString> playlistVideoIds(cJSON* root, int cap);

} // namespace parse
} // namespace youtube
} // namespace qxv
