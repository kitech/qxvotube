#ifndef TVLIST_H
#define TVLIST_H

/*
 * 网络电视内置清单（直播 HLS 流）：固定数据,无请求、无认证。
 * - 地区 key 固定四类(china/hongkong/asia/international),导航按地区分类;
 * - mustProxy=true 的墙外频道走 qxv::effectiveProxy 代理策略(同 provider);
 * - cover 有则填台标 URL,交 thumbcache 按网络结果加载,空则 identicon 兜底;
 * - 码流仅 master m3u8 才有,单流源填 "—"(不伪造);
 * - 清单随流地址漂移可能失效,属预期(直播源特性)。
 * Qt3/4/6 通用：仅用 QString/std::vector,无 UI 依赖。
 */

#include "qlcompat34.h"
#include <qstring.h>
#include <vector>

struct TvChannel
{
    const char* region;      /* "china" / "hongkong" / "asia" / "international" */
    const char* country;     /* 国家,如 "中国" / "德国" / "日本" */
    const char* name;        /* 频道名(UTF-8) */
    const char* url;         /* 直播流地址(HLS/m3u8) */
    const char* cover;       /* 台标/封面 URL,空 = identicon 兜底 */
    const char* resolution;  /* 分辨率 "1080p" / "720p" / "—" */
    const char* bitrate;     /* 码流 "3.2 Mbps" / "—"(单流无此信息) */
    bool        hls;         /* true = .m3u8 (HLS) */
    bool        mustProxy;   /* true = 墙外频道走代理 */
};

namespace tvlist {

/* 地区 key 集合(固定顺序 = 显示顺序) */
const char* const* regions(int& count);

/* 地区 i18n key,如 "tv.region.cn" */
QString regionTitleKey(const QString& regionKey);

/* 全量清单 */
const std::vector<TvChannel>& stations();

/* 按地区过滤(regionKey 为空 = 全部) */
std::vector<TvChannel> byRegion(const QString& regionKey);

} // namespace tvlist

#endif
