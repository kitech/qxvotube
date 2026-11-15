#pragma once
#include "common/compat.h"

#include <QList>
#include <QString>
#include <functional>

// yt-dlp 可选后端(2026-11-15):
//   本机实测 TVHTML5/WEB 全口径都过不了 bot 校验,但 yt-dlp 能拿到完整
//   adaptive 直链(2160p 视频 + m4a/webm 音频)。因此把 yt-dlp 作为
//   **可选依赖**:存在则用于 DASH(高清晰度)取流;不存在自动回落 itag18。
//   开关:QXVOTUBE_YT_YTDLP=0 强制关闭;=1 强制开启(缺失则回退)。
namespace qxv {
namespace ytdlp {

struct Format {
    QString id;        // format_id
    int height = 0;    // 视频高度(音频为 0)
    QString ext;
    QString proto;     // https / m3u8_native ...
    QString vcodec;    // "none" = 纯音频
    QString acodec;    // "none" = 纯视频
    double abr = 0;    // 音频码率
    double tbr = 0;
    QString url;
    bool hasUrl() const { return !url.isEmpty(); }
    bool videoOnly() const { return acodec == qFromUtf8("none") && height > 0; }
    bool audioOnly() const { return vcodec == qFromUtf8("none"); }
};

struct Info {
    bool ok = false;
    QString err;
    QString title;
    QString hlsMaster;   // HLS 多码率主播放列表(hls_variant),含音频
    QList<Format> formats;
};

// 是否可用:显式 env 优先;否则探测 PATH 里是否有 yt-dlp
bool enabled();

// 异步执行 `yt-dlp -J`(回调在调用方线程;需事件循环)
void fetch(const QString& url, const QString& proxy,
           std::function<void(const Info&)> cb);

} // namespace ytdlp
} // namespace qxv
