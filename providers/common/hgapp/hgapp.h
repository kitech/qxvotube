#pragma once
#include "common/compat.h"
#include "common/netclient.h"
#include "common/result.h"

// 红果 App 通道(2026-11-15 实测:零签名、零真机,见 docs/protocols/bytedance.md §6.1):
//   ① device_register 信封格式 POST log.snssdk.com/service/2/device_register/
//      body = {"magic_tag":"ss_app_log","header":{...},"_gen_time":ms}
//      → device_id / install_id / device_token(进程内缓存;env 可覆盖)
//   ② 取流 POST api5-normal-sinfonlineb.fqnovel.com/novel/player/multi_video_model/v1/
//      body = {"dr_scene":"preload",
//              "biz_param":{"video_platform":1024,"need_all_video_definition":true},
//              "mixed_video_id_map":{"1":[vid]}}
//      → 5 档轨道(360p~1080p),每档带 main_url(CENC 密文 mp4)+ encrypt_info.spade_a
// 注意:
// - 单集 video_model/v1 已停服(HTTP 200 空包),多集端点必须带 mixed_video_id_map;
// - video_platform 只有 1024 合法(其余返回 code=100001 "video platform invalid");
// - definition 值带 p 后缀("1080p");1080p=bytevc1(HEVC 可播),<=720p=bytevc2(私有编码);
// - 同一集全部清晰度共用同一 content key(spade_a 同值);
// - 空响应(200 + Content-Length 0)必须显式报错,不静默返回空列表。
namespace qxv {
namespace hgapp {

struct Device {
    QString deviceId;
    QString installId;
    QString deviceToken;
    bool valid() const {
        // "0" = 注册被服务端拒(实测并发注册时会出现),必须当无效
        return !deviceId.isEmpty() && deviceId != qFromUtf8("0")
            && !installId.isEmpty() && installId != qFromUtf8("0");
    }
};

struct Track {
    QString definition;     // "1080p"/"720p"/...
    QString codecType;      // bytevc1(HEVC) / bytevc2(私有)
    QString url;            // main_url(CENC 密文)
    QString spadeA;         // encrypt_info.spade_a(base64)
    int width = 0;
    int height = 0;
    long long urlExpire = 0;
    bool playable() const { return codecType == qFromUtf8("bytevc1"); }
};

// 设备注册(信封体);成功后缓存。env 覆盖优先:
// QXVOTUBE_HG_DEVICE_ID / QXVOTUBE_HG_INSTALL_ID(真机抓包可用)
void ensureDevice(std::function<void(const Result&, const Device&)> cb);

// 取某集全部清晰度轨道(已解出 video_model JSON 字符串嵌套)。
// 内置 10 分钟 tracks 缓存(命中即时回调)。
// 200+空包 = 端点关闭(见 docs/providers/ui-integration.md §11):首个空包后熔断
// 10 分钟,期间直接返回失败让 provider 走 web 兜底;QXVOTUBE_HG_APP_PROBE=1
// 可放行一次探针请求,成功即解锁。
void videoModel(const QString& vid,
                std::function<void(const Result&, const QList36<Track>&)> cb);

// 多集批量(一次请求 mixed_video_id_map);用于详情后预热前 N 集
void videoModels(const QStringList& vids,
                 std::function<void(const Result&,
                                    const QMap<QString, QList36<Track>>&)> cb);

// 便捷:挑最高可播档(bytevc1 优先,否则最高分辨率)。
const Track* pickBest(const QList36<Track>& tracks);

// 进程内缓存(测试/多 provider 共享)
Device cachedDevice();
void setDevice(const Device& d);

} // namespace hgapp
} // namespace qxv
