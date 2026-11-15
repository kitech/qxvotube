#include "common/provider.h"
#include "bilibili/bilibiliprovider.h"
#include "youtube/youtubeprovider.h"

namespace qxv {

QList<VideoProvider*> createProviders() {
    return { new BilibiliProvider(), new YoutubeProvider() };
}

} // namespace qxv
