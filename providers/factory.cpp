#include "common/provider.h"
#include "bilibili/bilibiliprovider.h"
#include "youtube/youtubeprovider.h"
#include "hongguo/hongguoprovider.h"
#include "douyin/douyinprovider.h"

namespace qxv {

QList<VideoProvider*> createProviders() {
    return { new BilibiliProvider(), new YoutubeProvider(), new HongguoProvider(),
             new DouyinProvider() };
}

} // namespace qxv
