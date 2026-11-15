#include "common/provider.h"
#include "bilibili/bilibiliprovider.h"
#include "youtube/youtubeprovider.h"
#include "hongguo/hongguoprovider.h"
#include "douyin/douyinprovider.h"
#include "xiaohongshu/xiaohongshuprovider.h"

namespace qxv {

QList<VideoProvider*> createProviders() {
    return { new BilibiliProvider(), new YoutubeProvider(), new HongguoProvider(),
             new DouyinProvider(), new XiaohongshuProvider() };
}

} // namespace qxv
