#pragma once
#include "compat.h"
#include <QString>
#include <QVariantMap>
#include <QList>
#include <functional>

namespace qxv {

struct Stream {
    QString url;
    QString quality;        // "1080P" / "720P" ...
    QString kind;           // "mp4" / "hls" / "dash" / "encrypted"(需 mediacrypt/mpv key)
    QVariantMap extra;      // decryptionKey / headers / 码率等平台特有
};

struct VideoItem {
    QString id, title, author, uid, cover;
    qint64 durationMs = 0;
    QVariantMap extra;      // 平台特有(如红果 series_id、B站 bvid/cid)
};

struct PlayResult {
    QList<Stream> streams;
    VideoItem item;
};

struct CommentItem { QString id, author, text; qint64 time = 0; };

// 错误分类(透传规则:中间层只附加 detail,不得覆盖底层 code)
enum class Err {
    Ok = 0,
    // 网络/协议层
    Network,          // 连不上/超时/DNS(代理未配置也归此)
    Tls,              // 证书/握手失败(代理劫持、系统时间错误)
    Http,             // 非 2xx(detail.httpStatus 带状态码)
    Parse,            // 返回结构对不上(接口改版/签名错空 body 也归此+RiskHint)
    // 风控层
    RateLimit,        // 限频/封禁(412、200空body、400002)
    CaptchaRequired,  // 需要人机验证(detail.captchaUrl)
    RiskHint,         // 疑似风控但未确认(签名向量自检失败等)
    // 业务/内容层
    NotFound,         // 视频不存在/已删除
    NeedAuth,         // 需登录(游客不可见内容)
    AuthExpired,      // 凭证过期/失效
    Paywalled,        // 付费/会员内容(明确不绕过)
    GeoBlocked,       // 地区限制
    Unsupported,      // provider 未实现的能力
    Cancelled,
};

struct Result {
    Err code = Err::Ok;
    QString message;        // 面向开发者的技术信息(进日志,不上屏)
    QVariantMap detail;     // httpStatus/platform/captchaUrl/stage 等

    static Result ok() { return { Err::Ok, QString(), QVariantMap() }; }
    static Result fail(Err e, const QString& msg) { return { e, msg, QVariantMap() }; }
    static Result fail(Err e, const QString& msg, const QVariantMap& d) {
        return { e, msg, d };
    }
    bool retriable() const;   // Network/RateLimit/Http(5xx) = true
};

using SearchCb   = std::function<void(Result, const QList<VideoItem>&, bool hasMore)>;
using DetailCb   = std::function<void(Result, const VideoItem&, const QList<VideoItem>&)>;
using ResolveCb  = std::function<void(Result, const PlayResult&)>;
using CommentsCb = std::function<void(Result, const QList<CommentItem>&,
                                      const QString& nextCursor)>;
using ActionCb   = std::function<void(Result)>;

} // namespace qxv
