// a_bogus 离线对拍用例 —— 与 node 参考实现金标字节级比对
// 金标生成方法(可复现,见 docs/providers/protocols/bytedance.md §15):
//   node 侧:ab_sm3.js + ab_utils.js(参考原版,Apache-2.0) + harness:
//     - Math.random 替换为确定性 LCG(seed*1103515245+12345)/2^32
//     - Date 覆写为固定 now(Date.now 与 new Date 均固定)
//     - enterPageTs 显式设为 now(G_RELEASE 下 makeABogus 的 ts 参数不生效!)
//     - navigator.userAgent/window 尺寸/navigator.platform 按用例给定
//     - programVersion = G_RELEASE(312 行处,默认 G_DEBUG 会多打印)
//   本文件金标即上述 harness 输出,勿手改;上游换版本需重新生成并全对拍。
// 编译:g++ -O1 -std=c++11 -I.. -o abogus_golden_test abogus_golden_test.cpp abogus.cpp && ./abogus_golden_test
// 本文件不进 providers.pri(独立 main,仅离线对拍用)。
#include "abogus.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

unsigned int g_seed;
double lcg(void*) {
    g_seed = (unsigned int)((unsigned long long)g_seed * 1103515245ull + 12345ull);
    return g_seed / 4294967296.0;
}

const char* kUri1 = "device_platform=webapp&aid=6383&channel=channel_pc_web&keyword=%E7%BE%8E%E5%A5%B3&search_channel=aweme_video_general&sort_type=0&publish_time=0&offset=0&count=20&pc_client_type=1&cookie_enabled=true&screen_width=1920&screen_height=1080&browser_language=zh-CN&browser_platform=Win32&browser_name=Chrome&browser_version=135.0.0.0&browser_online=true&engine_name=Blink&engine_version=135.0.0.0&os_name=Windows&os_version=10&cpu_core_num=16&device_memory=8&platform=PC&downlink=10&effective_type=4g&round_trip_time=50&webid=7446688975144207935&msToken=BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBdhzx";
const char* kUri2 = "device_platform=webapp&aid=6383&channel=channel_pc_web&aweme_id=7322042668907842867&pc_client_type=1&version_code=170400&version_name=17.4.0&cookie_enabled=true&screen_width=1920&screen_height=1080&browser_language=zh-CN&browser_platform=Win32&browser_name=Chrome&browser_version=135.0.0.0&browser_online=true&engine_name=Blink&engine_version=135.0.0.0&os_name=Windows&os_version=10&cpu_core_num=16&device_memory=8&platform=PC&downlink=10&effective_type=4g&round_trip_time=50&webid=7446688975144207935&msToken=CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCdhzx";
const char* kUa1 = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
const char* kUa2 = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36";

struct Case { unsigned int seed; long long now; const char* uri; const char* ua; const char* fp; const char* golden; };
const Case kCases[] = {
    { 42u, 1735689600000LL, kUri2, kUa1, "1920|1080|1920|1040|1920|1040|0|0|Win32",
      "mXUfDF6EOxARKdKtYCbYScxUJ9nlrTSyCqTQRj9KtNKuTZebxuPFiNbmbow64xDhGRBsiK17dDl/YEVc/IUs1AHpwmkDuYwWesC5VyfoMqqRGzkQEHRhCwuNeJGOMQvwYAKSJlvVUU8O2V/1hpasUQ57C/aN4O4pWNx6kkjieV-Z6BXG7km=" },
    { 987654321u, 1700000000000LL, kUri1, kUa2, "2560|1440|2560|1400|2560|1400|0|0|Win32",
      "QJUfDwX7xq8RFdKtuOcI9n1UnHjMrs8yfpToRxoKeNuwKHFPqYY2uNc2cxoUs/jRs8piho1HDEU/YdVcTxU03/9kqmkfSOwWBUCn9tXL21q6YzJQEHfwez4NeJGb8OkE8/K6JAg6UU/cIdK4LprzUd5Jt/PxsOTpbrxbkDjiSV41gBXG7Dy=" },
    { 7u, 1666666666666LL, kUri1, kUa1, "1920|1080|1920|1040|1920|1040|0|0|Win32",
      "EJsfDwUiOd85PdKtuKj8SSol46IlrP8yy3ToRxNueNu7twMT-mY-NxtjJoF6sNYzGbphwF17BDllYdVcKdUz319pzmkDSztReT9nIyso0qqRGFJQDHRsCwuN9JaalOiwmQojJI6V1U/c2VK1i3aiUBA7S/an4YGpWrxjkm7i7V4I6BtG19/YSsy=" },
    { 4294967295u, 1893456000000LL, kUri2, kUa2, "2560|1440|2560|1400|2560|1400|0|0|Win32",
      "dXUfDw6Jxq8RPVKtYOnc99dlbCjMrBWycrT/RJHKCNuzxwFbjSizDxaLJxLW48jbumB0keI7kEl/YdVckdU01AnpqmkfuYhW/U9AnwXo21whb0vQErfkeuWiHJaYWYJEu5oRJ1k6XU/a2dA4LprkUplrS/TxsO4pSNObkkyNtV426shG7d6=" },
    { 1u, 1000000000000LL, kUri1, kUa1, "2560|1440|2560|1400|2560|1400|0|0|Win32",
      "QysfDFtLmdRRPdKtYCjESuHUnHj/rsuy63ToRPNKeNK79qATGYimYPagboq64QfsMSBhkC17QDU/bEncXdXh3IrDKmkkuQXWTGCCnwfoZ1q6b0iQgNRzewRNeJGTWmvE85oyJAkIlU8O2EC4D3asUpAr7/Pn4YspWHOfks7iCV41gsXG7/Y=" },};

} // namespace

int main() {
    int ok = 0;
    const int n = (int)(sizeof(kCases) / sizeof(kCases[0]));
    for (int i = 0; i < n; ++i) {
        g_seed = kCases[i].seed;
        std::string out = qxv::abogus::generate(kCases[i].uri, kCases[i].ua,
                                                kCases[i].now, 0, lcg, kCases[i].fp);
        bool match = out == kCases[i].golden;
        std::printf("case%d %s (len=%d)\n", i, match ? "MATCH" : "MISMATCH", (int)out.size());
        if (!match) {
            std::printf("  got    %s\n", out.c_str());
            std::printf("  golden %s\n", kCases[i].golden);
        } else {
            ++ok;
        }
    }
    std::printf("%d/%d\n", ok, n);
    return ok == n ? 0 : 1;
}
