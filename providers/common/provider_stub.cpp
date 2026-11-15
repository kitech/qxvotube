// Qt3/Qt4 构建用的空注册表(providers 实现只编 Qt>=5):
// UI 调 createProviders() 得到空表 → 隐藏"视频源"入口,不伪造能力。
// 真实实现在 Qt>=5 由 providers.pri / CMakeLists.txt 编入。
#include "provider.h"

namespace qxv {

QList<VideoProvider*> createProviders() {
    return QList<VideoProvider*>();
}

} // namespace qxv
