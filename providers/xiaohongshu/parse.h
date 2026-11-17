#pragma once
#include "common/compat.h"
#include "common/result.h"
#include "common/provider.h"
#include "common/qcjson.h"

#include <QList>

// 小红书 SSR 结构解析(纯函数,供 provider 与离线 fixture 单测共用)。
// 口径见 docs/providers/xiaohongshu-research.md。
namespace qxv {
namespace xiaohongshu {
namespace parse {

// 首页 /explore:`loaderData`? 否 → 根下 feed.feeds[]
// 条目:{id, xsecToken, noteCard:{type, displayTitle, user{userId,nickname},
//        cover{urlDefault,urlPre,infoList[].url}, interactInfo{likedCount}}}
QList<VideoItem> feedNotes(cJSON* root);

// 用户页 /user/profile/<uid>:user.notes 是"tab 数组的数组"
QList<VideoItem> userNotes(cJSON* root);

// 详情 /explore/<id>:note.noteDetailMap[?].note
// 成功:vi + streams(视频笔记);图文笔记 → Err::Unsupported
Result noteDetail(cJSON* root, const QString& noteId, VideoItem* vi,
                  QList<Stream>* streams);

// 从分享链接(或任意 URL)中解出 noteId 与 xsec_token
bool parseShareUrl(const QString& url, QString* noteId, QString* xsecToken);

// SSR 里探测频道列表(候选键 channel_list/channels/channel/homefeedChannel;
// 条目含 id|channel_id + name|channel_name)。探测不到 → 空,不猜。
QList<CategoryItem> channelList(cJSON* root);

} // namespace parse
} // namespace xiaohongshu
} // namespace qxv
