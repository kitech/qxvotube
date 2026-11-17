#include "hgapp.h"

#include "common/debuglog.h"
#include "common/qcjson.h"
#include "common/autherror.h"

#include <qdatetime.h>

#include <qmap.h>

#include <random>

// 口径见 hgapp.h 顶部注释 + docs/protocols/bytedance.md §6.1(2026-11-15 线上实测)。

namespace qxv {
namespace hgapp {

namespace {

const char* kMod = "hongguo-app";
const char* kRegHost = "https://log.snssdk.com";
const char* kApiHost = "https://api5-normal-sinfonlineb.fqnovel.com";
// 真机口径 UA(com.phoenix.read 7.3.5.32;Cronet/TTNet 版本对齐 dong/fun 2026-11)
const char* kUa =
    "com.phoenix.read/73532 (Linux; U; Android 16; zh_CN; 25053RT47C; "
    "Build/BP2A.250605.031.A3; Cronet/TTNetVersion:04657795 2026-01-23 "
    "QuicVersion:c67e9834 2025-09-08)";

Device g_dev;
// 注册合流:并发 ensureDevice 只真正注册一次,其余排队等结果
bool g_registering = false;
QList36<std::function<void(const Result&, const Device&)>> g_waiters;

std::mt19937& randGen() {
    static std::mt19937 g(static_cast<unsigned>(
        std::random_device{}() ^ static_cast<unsigned>(wallMs())));
    return g;
}

QString randHex(int n) {
    static const char* kHex = "0123456789abcdef";
    QByteArray s;
    std::string out;
    out.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        out.push_back(kHex[randGen()() % 16]);
    return qFromUtf8(out.data(), (int)out.size());
}

qint64 nowMs() { return wallMs(); }

// 公共 query(游客画像;device_id/install_id 来自 ensureDevice 缓存或 env)
QString commonQuery(const Device& d) {
    QUrlQuery q;
    q.addQueryItem(qFromUtf8("aid"), qFromUtf8("8662"));
    q.addQueryItem(qFromUtf8("app_name"), qFromUtf8("novelread"));
    q.addQueryItem(qFromUtf8("version_code"), qFromUtf8("73532"));
    q.addQueryItem(qFromUtf8("version_name"), qFromUtf8("7.3.5.32"));
    q.addQueryItem(qFromUtf8("manifest_version_code"), qFromUtf8("73532"));
    q.addQueryItem(qFromUtf8("update_version_code"), qFromUtf8("73532"));
    q.addQueryItem(qFromUtf8("channel"), qFromUtf8("update_64"));
    q.addQueryItem(qFromUtf8("device_platform"), qFromUtf8("android"));
    q.addQueryItem(qFromUtf8("os"), qFromUtf8("android"));
    q.addQueryItem(qFromUtf8("ssmix"), qFromUtf8("a"));
    q.addQueryItem(qFromUtf8("device_type"), qFromUtf8("25053RT47C"));
    q.addQueryItem(qFromUtf8("device_brand"), qFromUtf8("Redmi"));
    q.addQueryItem(qFromUtf8("language"), qFromUtf8("zh"));
    q.addQueryItem(qFromUtf8("os_api"), qFromUtf8("36"));
    q.addQueryItem(qFromUtf8("os_version"), qFromUtf8("16"));
    q.addQueryItem(qFromUtf8("resolution"), qFromUtf8("1280*2772"));
    q.addQueryItem(qFromUtf8("dpi"), qFromUtf8("520"));
    q.addQueryItem(qFromUtf8("device_id"), d.deviceId);
    q.addQueryItem(qFromUtf8("install_id"), d.installId);
    q.addQueryItem(qFromUtf8("ac"), qFromUtf8("wifi"));
    q.addQueryItem(qFromUtf8("openudid"), qFromUtf8("hglive0000000001"));
    q.addQueryItem(qFromUtf8("cdid"), qFromUtf8("hglivecdid0000000000000000000001"));
    q.addQueryItem(qFromUtf8("_rticket"), QString::number(nowMs()));
    return q.toString();
}

NetClient& net() {
    static NetClient c;
    return c;
}

// ── device_register(信封格式,无签名)────────────────────────────────
void doRegister(std::function<void(const Result&, const Device&)> cb) {
    const QByteArray openudid = qToUtf8(randHex(16));
    const QByteArray clientudid = qToUtf8(randHex(16));
    const QByteArray cdid = qToUtf8(randHex(32));

    cJSON* root = cJSON_CreateObject();
    cJSON* hdr = cJSON_CreateObject();
    cJSON_AddStringToObject(hdr, "device_id", "0");
    cJSON_AddStringToObject(hdr, "server_id", "0");
    cJSON_AddStringToObject(hdr, "os", "Android");
    cJSON_AddStringToObject(hdr, "os_version", "16");
    cJSON_AddStringToObject(hdr, "os_api", "36");
    cJSON_AddStringToObject(hdr, "device_model", "25053RT47C");
    cJSON_AddStringToObject(hdr, "device_brand", "Redmi");
    cJSON_AddStringToObject(hdr, "device_type", "25053RT47C");
    cJSON_AddStringToObject(hdr, "language", "zh");
    cJSON_AddStringToObject(hdr, "region", "CN");
    cJSON_AddStringToObject(hdr, "tz_name", "Asia/Shanghai");
    cJSON_AddStringToObject(hdr, "tz_offset", "28800");
    cJSON_AddStringToObject(hdr, "resolution", "1280*2772");
    cJSON_AddStringToObject(hdr, "dpi", "520");
    cJSON_AddStringToObject(hdr, "cpu_model", "SM8650");
    cJSON_AddStringToObject(hdr, "cpu_arch", "arm64");
    cJSON_AddStringToObject(hdr, "build_serial", "BP2A.250605.031.A3");
    // 关键:display_name / app_name 必须是 phoenix_read(扁平 payload 会被拒)
    cJSON_AddStringToObject(hdr, "display_name", "phoenix_read");
    cJSON_AddStringToObject(hdr, "app_name", "phoenix_read");
    cJSON_AddStringToObject(hdr, "version_code", "73532");
    cJSON_AddStringToObject(hdr, "app_version", "7.3.5.32");
    cJSON_AddStringToObject(hdr, "channel", "update_64");
    cJSON_AddStringToObject(hdr, "aid", "8662");
    cJSON_AddStringToObject(hdr, "device_platform", "android");
    cJSON_AddStringToObject(hdr, "user_agent", kUa);
    cJSON_AddStringToObject(hdr, "openudid", openudid.data());
    cJSON_AddStringToObject(hdr, "clientudid", clientudid.data());
    cJSON_AddStringToObject(hdr, "cdid", cdid.data());
    cJSON_AddItemToObject(root, "header", hdr);
    cJSON_AddStringToObject(root, "magic_tag", "ss_app_log");
    cJSON_AddNumberToObject(root, "_gen_time", static_cast<double>(nowMs()));
    char* s = cJSON_PrintUnformatted(root);
    const QByteArray body = qbaFromCharptr(s);
    cJSON_free(s);
    cJSON_Delete(root);

    QUrlQuery q;
    q.addQueryItem(qFromUtf8("aid"), qFromUtf8("8662"));
    q.addQueryItem(qFromUtf8("app_name"), qFromUtf8("phoenix_read"));
    q.addQueryItem(qFromUtf8("version_code"), qFromUtf8("73532"));
    q.addQueryItem(qFromUtf8("device_platform"), qFromUtf8("android"));

    NetRequest req;
    req.method = qFromUtf8("POST");
    req.url = qFromUtf8("%1/service/2/device_register/?%2")
                  .arg(qFromUtf8(kRegHost), q.toString());
    req.headers = {
        { "Content-Type", "application/json; charset=utf-8" },
        { "User-Agent", kUa },
    };
    req.body = body;
    req.timeoutSec = 20;
    req.maxAttempts = 2;      // 注册失败(5xx/超时)重试一次
    req.retryDelayMs = 400;
    DBG_P(kMod, Debug, "device_register (%d bytes)", body.size());

    net().send(req, [cb](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                    ? qFromUtf8("register http %1").arg(r.status) : r.error),
               Device());
            return;
        }
        if (r.body.isEmpty()) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("register: empty body (风控或接口改版)")), Device());
            return;
        }
        JRoot root(r.body);
        Device d;
        d.deviceId = jstr(root.p, "device_id_str");
        if (d.deviceId.isEmpty())
            d.deviceId = QString::number(jint(root.p, "device_id"));
        d.installId = jstr(root.p, "install_id_str");
        if (d.installId.isEmpty())
            d.installId = QString::number(jint(root.p, "install_id"));
        d.deviceToken = jstr(root.p, "device_token");
        if (!d.valid()) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("register: no device_id/install_id")), Device());
            return;
        }
        g_dev = d;
        DBG_P(kMod, Info, "device registered: did=%s iid=%s new_user=%lld",
              qToUtf8(d.deviceId).data(), qToUtf8(d.installId).data(),
              jint(root.p, "new_user"));
        cb(Result::ok(), d);
    });
}

} // namespace

Device cachedDevice() { return g_dev; }
void setDevice(const Device& d) { g_dev = d; }

static void finishRegister(const Result& r, const Device& d) {
    g_registering = false;
    const QList36<std::function<void(const Result&, const Device&)>> waiters =
        g_waiters;
    g_waiters.clear();
    for (int i = 0; i < waiters.size(); ++i) waiters[i](r, d);
}

void ensureDevice(std::function<void(const Result&, const Device&)> cb) {
    // env 覆盖优先(真机抓包可用)
    const QString envDid = qenv("QXVOTUBE_HG_DEVICE_ID");
    const QString envIid = qenv("QXVOTUBE_HG_INSTALL_ID");
    if (!envDid.isEmpty() && !envIid.isEmpty()) {
        if (g_dev.deviceId != envDid || g_dev.installId != envIid) {
            g_dev.deviceId = envDid;
            g_dev.installId = envIid;
            DBG_P(kMod, Info, "device from env: did=%s", qToUtf8(envDid).data());
        }
        cb(Result::ok(), g_dev);
        return;
    }
    if (g_dev.valid()) { cb(Result::ok(), g_dev); return; }
    if (g_registering) {   // 已有注册在途:排队,避免并发注册(服务端会给 0)
        DBG_P(kMod, Debug, "device_register in-flight, queue waiter (%d)",
              g_waiters.size() + 1);
        g_waiters.append(std::move(cb));
        return;
    }
    g_registering = true;
    doRegister([cb](const Result& r, const Device& d) {
        if (r.code == Err::Ok) {
            g_dev = d;   // 只在成功时落缓存(失败不污染)
        }
        cb(r, d);
        finishRegister(r, d);
    });
}

namespace {

// tracks 缓存:vid → (tracks, 过期时间);10 分钟(CDN 6h,取短更安全)
struct TrackCacheEntry {
    QList36<Track> tracks;
    qint64 expiresAt = 0;
};
QMap<QString, TrackCacheEntry>& trackCache() {
    static QMap<QString, TrackCacheEntry> m;
    return m;
}
const qint64 kTrackTtlMs = 10 * 60 * 1000;

// ── App 链条熔断(2026-11-17 加)──────────────────────────────────────
// 背景:multi_video_model 稳定返回 200 + 0 字节 = 服务端对匿名/游客身份静默关闭。
//   依据 docs/providers/ui-integration.md §11 的 5 组对照(身份/字段/body/端点/
//   出口全试过都是空),以及同源社区 2026-10-21 用"公开游客画像"短暂恢复、
//   2026-11 又全空的记录。所以这不是本地参数能修的问题。
// 动机:每次点播都先打一遍必然空的请求,白付 1~2 个往返;代理抖动时还会叠加
//   detail/fallback-play-info 的超时(实测单次点播最长 35s)。首个空包后熔断
//   10 分钟,provider 收到失败立即走 web 360p 兜底,不再等 App 链条。
// 恢复探测:QXVOTUBE_HG_APP_PROBE=1 时,熔断期内仍放行"一次"探针请求
//   (仍只有 1 次,不重试)。拿到轨道就解锁;再空则维持熔断,本会话不再探。
// 边界:只对"空包"熔断;传输错误/5xx 不触发,由 maxAttempts 正常重试。
const qint64 kAppBlockMs = 10 * 60 * 1000;   // 10 分钟
qint64 g_appBlockUntil = 0;                  // 0 = 未熔断
int g_probeState = 0;                        // 0 未用过 / 1 已放行 / 2 已用尽

bool probeEnabled() {
    const QString v = qenv("QXVOTUBE_HG_APP_PROBE");
    return !v.isEmpty() && v != qFromUtf8("0");
}

// true = 熔断生效,本次直接失败(不发任何请求,含 device_register)
bool appBlocked() {
    if (g_appBlockUntil == 0 || nowMs() >= g_appBlockUntil) return false;
    if (g_probeState == 0 && probeEnabled()) {
        g_probeState = 1;   // 本次放行作为探针
        DBG_P(kMod, Info, "app chain 熔断中,放行一次探针(剩余 %lld ms)",
              (long long)(g_appBlockUntil - nowMs()));
        return false;
    }
    DBG_P(kMod, Debug, "app chain 熔断中,跳过 App 取流(剩余 %lld ms)",
          (long long)(g_appBlockUntil - nowMs()));
    return true;
}

void appBlockTrip() {
    const bool wasBlocked = (g_appBlockUntil != 0 && nowMs() < g_appBlockUntil);
    g_appBlockUntil = nowMs() + kAppBlockMs;
    if (wasBlocked) return;    // 熔断期内的重复空包(并发在飞/探针失败):不续期日志、不重发探针
    g_probeState = 0;          // 新一轮熔断:允许(若 env 开启)一次探针
    // 并发在飞的多个请求会各回一个空包,只记一次,避免刷屏
    DBG_P(kMod, Warn,
          "app chain 熔断 10 分钟(200+空包);QXVOTUBE_HG_APP_PROBE=1 可复探");
}

void appBlockClear() {
    if (g_appBlockUntil == 0) return;
    g_appBlockUntil = 0;
    g_probeState = 2;
    DBG_P(kMod, Info, "app chain 探针成功,解除熔断");
}

// 从 video_model JSON 字符串解析轨道列表
bool parseTracks(const QString& vmStr, QList36<Track>* out, QString* err) {
    JRoot vm(qToUtf8(vmStr));
    if (!vm.ok()) { if (err) *err = qFromUtf8("video_model not json"); return false; }
    const cJSON* list = jarr(vm.p, "video_list");
    const long long expire = jint(vm.p, "url_expire");
    QList36<Track> tracks;
    for (int i = 0; i < jsize(list); ++i) {
        const cJSON* it = jat(list, i);
        Track t;
        const cJSON* meta = jobj(it, "video_meta");
        t.definition = jstr(meta, "definition");
        t.codecType = jstr(meta, "codec_type");
        t.width = int(jint(meta, "width"));
        t.height = int(jint(meta, "height"));
        t.url = jstr(it, "main_url");
        t.spadeA = jstr(jobj(it, "encrypt_info"), "spade_a");
        t.urlExpire = expire;
        if (!t.url.isEmpty() && !t.spadeA.isEmpty()) tracks.append(t);
    }
    if (tracks.isEmpty()) { if (err) *err = qFromUtf8("video_list 为空或缺 spade_a"); return false; }
    *out = tracks;
    return true;
}

} // namespace

void videoModels(const QStringList& vids,
                 std::function<void(const Result&,
                                    const QMap<QString, QList36<Track>>&)> cb) {
    if (vids.isEmpty()) { cb(Result::ok(), QMap<QString, QList36<Track>>()); return; }
    if (appBlocked()) {
        cb(Result::fail(Err::RateLimit,
           qFromUtf8("hongguo-app: 熔断中(端点已关闭,直接走 web 兜底)")), {});
        return;
    }
    ensureDevice([vids, cb](const Result& dr, const Device& d) {
        if (dr.code != Err::Ok) { cb(dr, {}); return; }
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "dr_scene", "preload");
        cJSON* bp = cJSON_CreateObject();
        cJSON_AddNumberToObject(bp, "video_platform", 1024);
        cJSON_AddBoolToObject(bp, "need_all_video_definition", 1);
        cJSON_AddItemToObject(root, "biz_param", bp);
        cJSON* map = cJSON_CreateObject();
        cJSON* arr = cJSON_CreateArray();
        for (int i = 0; i < vids.size(); ++i)
            cJSON_AddItemToArray(arr, cJSON_CreateString(qToUtf8(vids[i]).data()));
        cJSON_AddItemToObject(map, "1", arr);
        cJSON_AddItemToObject(root, "mixed_video_id_map", map);
        char* s = cJSON_PrintUnformatted(root);
        const QByteArray body = qbaFromCharptr(s);
        cJSON_free(s);
        cJSON_Delete(root);

        NetRequest req;
        req.method = qFromUtf8("POST");
        req.url = qFromUtf8("%1/novel/player/multi_video_model/v1/?%2")
                      .arg(qFromUtf8(kApiHost), commonQuery(d));
        req.headers = {
            { "Content-Type", "application/json; charset=UTF-8" },
            { "User-Agent", QString::fromUtf8(kUa) },
        };
        req.body = body;
        req.timeoutSec = 30;
        req.maxAttempts = 2;
        // 200+空包 = 服务端风控/端点关闭,重试无意义(1s 内会打 6 次);
        // 仅传输错误/5xx 仍按 maxAttempts 重试。
        req.retryOnEmpty = false;
        DBG_P(kMod, Debug, "video_models %d vids", vids.size());
        net().send(req, [cb, vids](const NetResponse& r) {
            if (r.status == 200 && r.body.isEmpty()) {
                appBlockTrip();   // 端点关闭:后续点播别再等这一跳
                cb(Result::fail(Err::RateLimit,
                   qFromUtf8("hongguo-app: 空响应(风控)")), {});
                return;
            }
            if (!r.ok()) {
                cb(Result::fail(Err::Network, r.error.isEmpty()
                       ? qFromUtf8("video_models http %1").arg(r.status)
                       : r.error), {});
                return;
            }
            JRoot root(r.body);
            if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}); return; }
            if (jint(root.p, "code", -1) != 0) {
                cb(Result::fail(Err::Parse,
                   qFromUtf8("hongguo-app: code=%1").arg(jint(root.p, "code"))), {});
                return;
            }
            QMap<QString, QList36<Track>> out;
            const cJSON* data = jobj(root.p, "data");
            for (int i = 0; i < vids.size(); ++i) {
                const cJSON* vd = jobj(data, qToUtf8(vids[i]).data());
                if (!vd) continue;
                QList36<Track> tracks;
                QString err;
                if (!parseTracks(jstr(vd, "video_model"), &tracks, &err)) continue;
                out.insert(vids[i], tracks);
                TrackCacheEntry e;
                e.tracks = tracks;
                e.expiresAt = nowMs() + kTrackTtlMs;
                trackCache().insert(vids[i], e);
            }
            DBG_P(kMod, Info, "video_models -> %d/%d cached", out.size(), vids.size());
            cb(Result::ok(), out);
        });
    });
}

void videoModel(const QString& vid,
                std::function<void(const Result&, const QList36<Track>&)> cb) {
    {
        QMap<QString, TrackCacheEntry>& c = trackCache();
        auto it = c.find(vid);
        if (it != c.end()) {
            if (nowMs() < (*it).expiresAt) {
                DBG_P(kMod, Debug, "video_model cache hit %s", qToUtf8(vid).data());
                cb(Result::ok(), (*it).tracks);
                return;
            }
            c.erase(it);
        }
    }
    if (appBlocked()) {
        cb(Result::fail(Err::RateLimit,
           qFromUtf8("hongguo-app: 熔断中(端点已关闭,直接走 web 兜底)")), {});
        return;
    }
    ensureDevice([vid, cb](const Result& dr, const Device& d) {
        if (dr.code != Err::Ok) { cb(dr, {}); return; }

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "dr_scene", "preload");
        cJSON* bp = cJSON_CreateObject();
        cJSON_AddNumberToObject(bp, "video_platform", 1024);   // 唯一合法值
        cJSON_AddBoolToObject(bp, "need_all_video_definition", 1);
        cJSON_AddItemToObject(root, "biz_param", bp);
        cJSON* map = cJSON_CreateObject();
        cJSON* arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateString(qToUtf8(vid).data()));
        cJSON_AddItemToObject(map, "1", arr);
        cJSON_AddItemToObject(root, "mixed_video_id_map", map);
        char* s = cJSON_PrintUnformatted(root);
        const QByteArray body = qbaFromCharptr(s);
        cJSON_free(s);
        cJSON_Delete(root);

        NetRequest req;
        req.method = qFromUtf8("POST");
        req.url = qFromUtf8("%1/novel/player/multi_video_model/v1/?%2")
                      .arg(qFromUtf8(kApiHost), commonQuery(d));
        req.headers = {
            { "Content-Type", "application/json; charset=UTF-8" },
            { "User-Agent", kUa },
            { "Accept", "application/json; charset=utf-8,x-protobuf" },
            { "x-xs-from-web", "0" },
            { "sdk-version", "2" },
        };
        req.body = body;
        req.timeoutSec = 25;
        req.maxAttempts = 3;      // 传输错误/5xx 退避重试(空包不重试)
        req.retryDelayMs = 500;
        // 200+空包 = 服务端风控/端点关闭,重试无意义(1s 内会打 6 次);
        // 仅传输错误/5xx 仍按 maxAttempts 重试。
        req.retryOnEmpty = false;
        DBG_P(kMod, Debug, "video_model vid=%s", qToUtf8(vid).data());

        net().send(req, [cb, vid](const NetResponse& r) {
            if (!r.ok()) {
                cb(Result::fail(Err::Network, r.error.isEmpty()
                       ? qFromUtf8("video_model http %1").arg(r.status)
                       : r.error), {});
                return;
            }
            if (r.body.isEmpty()) {
                // 200 + 空 body = 风控/接口关闭(单集端点的现状)
                appBlockTrip();
                cb(Result::fail(Err::RateLimit,
                   qFromUtf8("hongguo-app: 空响应(风控或端点关闭)")), {});
                return;
            }
            JRoot root(r.body);
            if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}); return; }
            const long long code = jint(root.p, "code", -1);
            if (code != 0) {
                cb(withServer(
                    Result::fail(Err::Parse,
                        qFromUtf8("hongguo-app: code=%1 %2")
                            .arg(code).arg(jstr(root.p, "message"))),
                    200, QString::number(code), jstr(root.p, "message")), {});
                return;
            }
            const cJSON* vd = jobj(jobj(root.p, "data"), qToUtf8(vid).data());
            if (!vd) {
                cb(Result::fail(Err::Parse,
                   qFromUtf8("hongguo-app: no data[%1]").arg(vid)), {});
                return;
            }
            QList36<Track> out;
            QString perr;
            if (!parseTracks(jstr(vd, "video_model"), &out, &perr)) {
                cb(Result::fail(Err::Parse,
                   qFromUtf8("hongguo-app: %1").arg(perr)), {});
                return;
            }
            {
                TrackCacheEntry e;
                e.tracks = out;
                e.expiresAt = nowMs() + kTrackTtlMs;
                trackCache().insert(vid, e);
            }
            appBlockClear();   // 探针拿到轨道 → 解除熔断
            DBG_P(kMod, Info, "video_model %s -> %d tracks (first %s/%s)",
                  qToUtf8(vid).data(), out.size(), qToUtf8(out[0].definition).data(),
                  qToUtf8(out[0].codecType).data());
            cb(Result::ok(), out);
        });
    });
}

const Track* pickBest(const QList36<Track>& tracks) {
    const Track* best = nullptr;
    for (int i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        if (!t.playable()) continue;      // bytevc2 私有编码,优先跳过
        if (!best || t.width > best->width) best = &t;
    }
    if (best) return best;
    for (int i = 0; i < tracks.size(); ++i)
        if (!best || tracks[i].width > best->width) best = &tracks[i];
    return best;
}

} // namespace hgapp
} // namespace qxv
