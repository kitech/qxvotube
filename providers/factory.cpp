#include "common/provider.h"
#include "bilibili/bilibiliprovider.h"
#include "youtube/youtubeprovider.h"
#include "hongguo/hongguoprovider.h"
#include "douyin/douyinprovider.h"
#include "xiaohongshu/xiaohongshuprovider.h"
#include "netflix/netflixprovider.h"

namespace qxv {

QList36<VideoProvider*> createProviders() {
    QList36<VideoProvider*> l;
    l.append(new BilibiliProvider());
    l.append(new YoutubeProvider());
    l.append(new HongguoProvider());
    l.append(new DouyinProvider());
    l.append(new XiaohongshuProvider());
    l.append(new NetflixProvider());
    return l;
}

} // namespace qxv
