#pragma once
#include "compat.h"
#include "common/provider.h"
#include "common/netclient.h"
#include "common/cookiejar.h"
#include "common/streamcache.h"

namespace qxv {

// bilibili:REST 接口(web 端游客可用口)
// 资料:bilibili-API-collect(search/x/web-interface, playurl v1)
// 认证:search/playurl 游客可用(低清晰度);高清/番剧需登录 → NeedAuth
class BilibiliProvider : public VideoProvider {
public:
    QString id() const override { return qFromUtf8("bilibili"); }

    void search(const QString& kw, int page, SearchCb cb) override;
    void homeFeed(int page, SearchCb cb) override;
    void categories(std::function<void(const Result&,
                                       const QList36<CategoryItem>&)>) override;
    void categoryFeed(const QString& key, int page, SearchCb cb) override;
    void detail(const QString& id, DetailCb cb) override;
    void comments(const QString& id, const QString& cursor,
                  CommentsCb cb) override;
    void resolve(const QString& id, ResolveCb cb) override;
    void resolveQuality(const QString& id, const QString& quality,
                        ResolveCb cb) override;
    // 字幕空壳:B站字幕需登录态(游客 /x/player/v2 subtitles=[]),本项目免登录主线 → 显式不支持
    void subtitles(const QString& id, SubtitlesCb cb) override {
        Q_UNUSED(id);
        cb(Result::fail(Err::Unsupported,
            qFromUtf8("B站字幕需登录态,游客态不支持")), QList36<SubtitleTrack>());
    }

    void setProxy(const ProxyConfig& p) override { m_proxy = p; }
    void setCredentials(const QVariantMap& c) override { m_cred = c; }
    void cancel() override;
    void verifyAuth(std::function<void(const Result&, const QString&)> cb) override;

private:
    void apiGet(const QString& path, const QString& query,
                std::function<void(const NetResponse&)> cb);
    void recommendFeed(int page, SearchCb cb);
    void dynamicFeed(int page, SearchCb cb);
    ProxyConfig m_proxy;
    QVariantMap m_cred;
    NetClient m_net;
    CookieJar m_jar;
    bool m_jarInit = false;
    QStringList m_dynOffsets;   // 个人动态分页游标(page N → N-1 返回的 offset)
    int m_rcmdFresh = 0;        // 首页推荐“换一换”批次(fresh_idx)
    bool m_warmTried = false;   // buvid3 预热是否已尝试(防止递归)
    streamcache::Cache m_cache;
};

} // namespace qxv
