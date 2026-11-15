#pragma once
#include <QString>
#include <functional>

// YouTube PO token 获取口(P2 风控对策):
// LOGIN_REQUIRED "Sign in to confirm you're not a bot" 是 IP 级封锁,
// 干净 IP 的 ANDROID_VR 裸通;被标记 IP 需要 PO token(BotGuard attestation)。
// 参考见 docs/providers/protocols/youtube.md(invidious PR#4789 口径:
// po_token 仅对 WEB client 生效,需搭配 visitorData)。
//
// 两级来源(优先级从高到低):
//  1. 静态配置:QXVOTUBE_YT_POTOKEN / QXVOTUBE_YT_VISITORDATA
//     (youtube-trusted-session-generator 生成,填同 IP 出口的值)
//  2. BGutil 自托管服务:GET <QXVOTUBE_YT_POTURL|http://127.0.0.1:4416>/
//     返回 JSON {"poToken":"...","visitorData":"..."}(兼容只返回字符串)
//
// 用法(provider 侧):
//   PotokenSource::fetch(cb) → 有 token:player 用 WEB client + 两字段;
//                             无 token:维持 ANDROID_VR。
namespace qxv {

struct Potoken {
    QString token;       // po_token(BotGuard 派生)
    QString visitorData; // 可空(BGutil 服务可能返回;静态配置建议同填)
    bool valid() const { return !token.isEmpty(); }
};

class PotokenSource {
public:
    typedef std::function<void(const Potoken&)> FetchCb;
    // 异步取 token(内部走 NetClient,不阻塞);失败回调空 Potoken(回退裸通)
    static void fetch(const FetchCb& cb);
};

} // namespace qxv
