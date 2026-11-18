#pragma once
#include "common/compat.h"

// 抖音 __ac_signature 纯算(2025-02 CSDN 145899104 逆向还原,2026-11-18 实测
// 有效):首页首访拿 __ac_nonce,带本签名的二跳首页才下发 ttwid/UIFID_TEMP。
// 纯数学(hash+位运算),无加密库/无 JS 引擎依赖;Qt3/Qt4 同码。
namespace qxv {
namespace douyin {

// timeSec:秒级时间戳; site:window.location.pathname 风格的站点段
// ("www.douyin.com/"); nonce:__ac_nonce cookie; ua:浏览器 UA(须与实际一致)。
QString acSignature(long long timeSec, const QString& site,
                    const QString& nonce, const QString& ua);

// Argus 校验签名:md5("<uifid>_<timestamp>_<SALT>_<query>") 的十六进制串。
// query = URL 查询串(除 x-secsdk-web-signature 自身,URLSearchParams 口径)。
QString argusSignature(const QString& uifid, const QString& timestamp,
                       const QString& query);

} // namespace douyin
} // namespace qxv
