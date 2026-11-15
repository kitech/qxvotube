#include "common/provider.h"
#include "bilibili/bilibiliprovider.h"
#include "youtube/youtubeprovider.h"
#include "hongguo/hongguoprovider.h"

namespace qxv {

QList<VideoProvider*> createProviders() {
    return { new BilibiliProvider(), new YoutubeProvider(), new HongguoProvider() };
}

} // namespace qxv
