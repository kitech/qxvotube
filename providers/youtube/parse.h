#pragma once
#include "common/compat.h"
#include "common/provider.h"
#include "common/qcjson.h"

#include <QList>
#include <QString>

// YouTube InnerTube 响应解析(纯函数,供 provider 与离线 fixture 单测共用)。
namespace qxv {
namespace youtube {
namespace parse {

// search 响应:videoRenderer(WEB) + compactVideoRenderer(ANDROID/IOS)
QList<VideoItem> searchItems(cJSON* root);

// 续页游标:continuationCommand.token / nextContinuationData.continuation /
// reloadContinuationData.continuation(任一,空=没有下一页)
QString continuation(cJSON* root);

// playlist browse:按 watchEndpoint{videoId,playlistId} 收集(去重,上限 cap)
QList<QString> playlistVideoIds(cJSON* root, int cap);

} // namespace parse
} // namespace youtube
} // namespace qxv
