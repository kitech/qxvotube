// 验证:Qt3/Qt4 下 include providers.pri 能编译;createProviders() 返回空表。
#include "common/provider.h"
#include <cstdio>

int main() {
    const QList<qxv::VideoProvider*> provs = qxv::createProviders();
#ifdef QT3_BUILD
    std::printf("providers=%d (Qt %s)\n", provs.count(), QT_VERSION_STR);
#else
    std::printf("providers=%d (Qt %s)\n", provs.size(), QT_VERSION_STR);
#endif
    return 0;
}
