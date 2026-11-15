#include "douyinprovider.h"
#include "common/qcjson.h"
#include "parse.h"
#include "common/debuglog.h"
#include "common/fingerprint.h"

#include <QUrlQuery>

// 抖音免签面(2026-11-15 实测,详见 bytedance.md §13):
//   热榜 GET https://www.douyin.com/aweme/v1/web/hot/search/list/
//        ?device_platform=webapp&aid=6383&channel=channel_pc_web
//        → data.word_list[] {word, hot_value, video_count, discuss_video_count,
//                            group_id, sentence_id, word_cover.url_list[]}
// 其余接口(搜索/详情/取流)需 a_bogus 或 Argus/uifid 签名 → 显式 Unsupported。

namespace qxv {

static const char* kMod = "douyin";
static const char* kDramaHint =
    "douyin: 该能力需 a_bogus/Argus 签名(当前仅热榜免签,见 "
    "docs/providers/protocols/bytedance.md §13)";

void DouyinProvider::cancel() { m_net.cancel(); }

void DouyinProvider::homeFeed(int page, SearchCb cb) {
    if (page > 1) {   // 热榜只有一屏(接口无分页参数)
        cb(Result::ok(), {}, false);
        return;
    }
    if (!m_jarInit) { m_net.setCookieJar(&m_jar); m_jarInit = true; }

    QUrlQuery q;
    q.addQueryItem(qFromUtf8("device_platform"), qFromUtf8("webapp"));
    q.addQueryItem(qFromUtf8("aid"), qFromUtf8("6383"));
    q.addQueryItem(qFromUtf8("channel"), qFromUtf8("channel_pc_web"));

    NetRequest req;
    req.url = qFromUtf8("https://www.douyin.com/aweme/v1/web/hot/search/list/?%1")
                  .arg(q.toString(QUrl::FullyEncoded));
    for (const auto& h : Fingerprint::chromeWeb(qFromUtf8("zh-CN,zh;q=0.9")).headers)
        req.headers.append(h);
    req.headers.append(qMakePair(qFromUtf8("Referer"),
                                 qFromUtf8("https://www.douyin.com/")));
    req.autoCookie = true;
    req.timeoutSec = 20;
    req.maxAttempts = 3;
    req.retryDelayMs = 300;
    if (m_proxy.enabled)
        req.proxy = qFromUtf8("http://%1:%2").arg(m_proxy.host).arg(m_proxy.port);
    DBG_P(kMod, Debug, "hot search list");

    m_net.send(req, [cb](const NetResponse& r) {
        if (!r.ok()) {
            cb(Result::fail(Err::Network, r.error.isEmpty()
                   ? qFromUtf8("http %1").arg(r.status) : r.error), {}, false);
            return;
        }
        JRoot root(r.body);
        if (!root.ok()) { cb(Result::fail(Err::Parse, "not json"), {}, false); return; }
        const long long code = jint(root.p, "status_code", -1);
        if (code != 0) {
            cb(Result::fail(Err::Parse,
               qFromUtf8("douyin: status_code=%1").arg(code)), {}, false);
            return;
        }
        const QList<VideoItem> items = douyin::parse::hotWords(root.p);
        DBG_P(kMod, Debug, "hot list -> %d items", items.size());
        if (items.isEmpty()) {
            cb(Result::fail(Err::RateLimit,
               qFromUtf8("douyin: 热榜空结果(风控或改版)")), {}, false);
            return;
        }
        cb(Result::ok(), items, false);
    });
}

void DouyinProvider::search(const QString& kw, int page, SearchCb cb) {
    Q_UNUSED(kw);
    Q_UNUSED(page);
    cb(Result::fail(Err::Unsupported, qFromUtf8(kDramaHint)), {}, false);
}

void DouyinProvider::detail(const QString& id, DetailCb cb) {
    Q_UNUSED(id);
    cb(Result::fail(Err::Unsupported, qFromUtf8(kDramaHint)), {}, {});
}

void DouyinProvider::resolve(const QString& id, ResolveCb cb) {
    Q_UNUSED(id);
    cb(Result::fail(Err::Unsupported, qFromUtf8(kDramaHint)), {});
}

} // namespace qxv
