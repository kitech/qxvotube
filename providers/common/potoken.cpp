#include "potoken.h"
#include "compat.h"
#include "qcjson.h"
#include "netclient.h"
#include "debuglog.h"

namespace qxv {

static const char* kMod = "potoken";

// 顺序:静态 env → BGutil HTTP;都拿不到回调空值(provider 回退 ANDROID_VR)
//
// BGutil(https://github.com/Brainicism/bgutil-ytdlp-pot-provider)真实接口:
//   POST {base}/get_pot  body {"content_binding"?: <visitorData>, "proxy"?: "http://.."}
//   → {"poToken":"...","contentBinding":"<visitorData>","expiresAt":...}
// 注意:
//  - contentBinding 就是本次 token 绑定的 visitorData,必须原样放进 innertube
//    context.client.visitorData(两者绑定,错配等于无 token);
//  - token 与**生成时的公网 IP** 绑定 → BGutil 必须走与被测请求相同的出口
//    (proxy 参数透传 QXVOTUBE_PROXY / 显式 QXVOTUBE_YT_POTOKEN_PROXY);
//  - GET / 只用于探活(ping),不能取 token。
void PotokenSource::fetch(const FetchCb& cb) {
    const QString envTok = qenv("QXVOTUBE_YT_POTOKEN");
    if (!envTok.isEmpty()) {
        Potoken p;
        p.token = envTok;
        p.visitorData = qenv("QXVOTUBE_YT_VISITORDATA");
        DBG_P(kMod, Info, "potoken from env (%d bytes)", p.token.length());
        cb(p);
        return;
    }

    // 显式 opt-in:未设 POTURL 时不探测本地 4416(默认走 ANDROID 裸通,
    // 免 25s 的 BotGuard 挑战;需要 WEB+PO token 时再配 QXVOTUBE_YT_POTURL)
    QString base = qenv("QXVOTUBE_YT_POTURL");
    if (base.isEmpty()) { cb(Potoken()); return; }
    if (base.endsWith(qFromUtf8("/"))) base.truncate(base.length() - 1);
    QString proxy = qenv("QXVOTUBE_YT_POTOKEN_PROXY");
    if (proxy.isEmpty()) proxy = qenv("QXVOTUBE_PROXY");

    // body:有代理则透传(BotGuard 挑战与 token 绑定都走该出口)
    cJSON* root = cJSON_CreateObject();
    if (!proxy.isEmpty())
        cJSON_AddStringToObject(root, "proxy", qToUtf8(proxy).data());
    char* s = cJSON_PrintUnformatted(root);
    const QByteArray body = qbaFromCharptr(s);
    cJSON_free(s);
    cJSON_Delete(root);

    static NetClient net;
    NetRequest req;
    req.method = qFromUtf8("POST");
    req.url = base + qFromUtf8("/get_pot");
    req.headers.append(qMakePair(qFromUtf8("Content-Type"), qFromUtf8("application/json")));
    req.body = body;
    req.timeoutSec = 45;   // BotGuard 挑战在慢代理下要几十秒
    DBG_P(kMod, Debug, "POST %s proxy=%s", qToUtf8(req.url).data(),
          proxy.isEmpty() ? "(direct)" : qToUtf8(proxy).data());
    net.send(req, [cb](const NetResponse& r) {
        Potoken p;
        if (r.ok() && !r.body.isEmpty()) {
            // 口径 1:BGutil {"poToken":..,"contentBinding":..}
            JRoot root(r.body);
            if (root.ok()) {
                p.token = jstr(root.p, "poToken");
                p.visitorData = jstr(root.p, "contentBinding");
                if (p.visitorData.isEmpty())
                    p.visitorData = jstr(root.p, "visitorData");
            }
            // 口径 2:直接返回 token 串(自建简化服务)
            if (!p.valid())
                p.token = qTrim(qFromUtf8(r.body.data(), (int)r.body.size()));
        }
        if (!p.valid())
            DBG_P(kMod, Info, "no potoken (status=%d), fallback ANDROID_VR",
                  r.status);
        else
            DBG_P(kMod, Info, "potoken from http (%d bytes, visitorData=%d)",
                  p.token.length(), p.visitorData.length());
        cb(p);
    });
}

} // namespace qxv
