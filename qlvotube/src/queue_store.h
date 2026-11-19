#pragma once
#include "qlcompat34.h"
#include "contentview.h"
#include <vector>

// 视频播放队列快照(~/.config/qlvotube/queue.json;qlmusic queue.json 模型)
// 仅保存视频队列:provider/chip/current + 卡片最小字段(id 用于重新 resolve)。
// 恢复策略:摆好队列但不自动播放;provider 不匹配则丢弃(调用方提示)。
namespace queue_store {

bool save(const QString& provider, const QString& chip, int current,
          const std::vector<VideoCard>& cards);

bool load(QString* provider, QString* chip, int* current,
          std::vector<VideoCard>* cards);

void clear();

} // namespace queue_store
