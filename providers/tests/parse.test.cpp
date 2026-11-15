// 离线解析单测(不联网,进 ctest):
//   录制的真实响应(fixtures/) → 各平台 parse::* → 断言关键字段。
// fixture 重录:tests/fixtures/capture.py(需外网;YouTube 需 QXVOTUBE_PROXY)。
#include "bilibili/parse.h"
#include "hongguo/parse.h"
#include "youtube/parse.h"
#include "douyin/parse.h"
#include "common/qcjson.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <cstdio>

using namespace qxv;

namespace {

int g_fail = 0;

void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qPrintable(got)); }
}

QByteArray load(const QString& name) {
    QFile f(QString::fromUtf8(FIXTURE_DIR) + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("[FAIL] fixture missing: %s\n", qPrintable(name));
        ++g_fail;
        return QByteArray();
    }
    return f.readAll();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    // ── bilibili ────────────────────────────────────────────────
    {
        JRoot r(load(QString::fromUtf8("bilibili_search.json")));
        check(r.ok(), "bilibili_search.json parses");
        const QList<VideoItem> items = bilibili::parse::searchResult(r.p);
        check(items.size() >= 2, "bilibili search items>=2",
              QString::number(items.size()));
        check(items.isEmpty() || items.first().id.startsWith(QString::fromUtf8("BV")),
              "bilibili search id=BV...",
              items.isEmpty() ? QString() : items.first().id);
    }
    {
        JRoot r(load(QString::fromUtf8("bilibili_view.json")));
        VideoItem vi;
        QList<VideoItem> parts;
        const Result res = bilibili::parse::detailFromView(r.p, &vi, &parts);
        check(res.code == Err::Ok && !vi.title.isEmpty(), "bilibili view title",
              vi.title.left(20));
        check(!parts.isEmpty() && parts.first().id.contains(QLatin1Char('.')),
              "bilibili part.id 复合形式",
              parts.isEmpty() ? QString() : parts.first().id);
        check(!parts.isEmpty()
                  && parts.first().extra.value(QString::fromUtf8("cid")).toString()
                         == parts.first().id.section(QLatin1Char('.'), 1),
              "bilibili part.extra.cid 与复合 id 一致");
    }

    // ── hongguo ────────────────────────────────────────────────
    {
        JRoot r(load(QString::fromUtf8("hongguo_search.json")));
        bool hasMore = false;
        const QList<VideoItem> items =
            hongguo::parse::searchList(r.p, 1, &hasMore);
        check(items.size() >= 2, "hongguo search items>=2",
              QString::number(items.size()));
        check(!items.isEmpty() && items.first().id.size() >= 15,
              "hongguo series_id 为长数字串",
              items.isEmpty() ? QString() : items.first().id);
    }
    {
        JRoot r(load(QString::fromUtf8("hongguo_home.json")));
        const QList<VideoItem> items = hongguo::parse::homeSections(r.p);
        check(items.size() >= 8, "hongguo home items>=8 (4 tabs)",
              QString::number(items.size()));
        check(!items.isEmpty()
                  && !items.first().extra.value(QString::fromUtf8("tab")).toString()
                          .isEmpty(),
              "hongguo home extra.tab 存在");
    }
    {
        JRoot r(load(QString::fromUtf8("hongguo_detail.json")));
        VideoItem vi;
        QList<VideoItem> parts;
        const Result res = hongguo::parse::seriesDetail(r.p, &vi, &parts);
        check(res.code == Err::Ok && parts.size() >= 3, "hongguo detail episodes>=3",
              QString::number(parts.size()));
        bool seriesOk = true;
        for (const VideoItem& p : parts)
            if (p.extra.value(QString::fromUtf8("seriesId")).toString() != vi.id)
                seriesOk = false;
        check(seriesOk && !vi.id.isEmpty(), "hongguo part.extra.seriesId 一致");
    }
    {
        // extractRouterData:HTML 包裹 + 非空终止解析
        const QByteArray html =
            "<html><script>window._ROUTER_DATA = {\"a\":{\"b\":1}};</script></html>";
        JRoot r;
        check(hongguo::parse::extractRouterData(html, r) && jobj(r.p, "a") != nullptr,
              "hongguo extractRouterData(HTML)");
    }

    // ── youtube ────────────────────────────────────────────────
    {
        JRoot r(load(QString::fromUtf8("youtube_search.json")));
        const QList<VideoItem> items = youtube::parse::searchItems(r.p);
        check(items.size() >= 2, "youtube search items>=2",
              QString::number(items.size()));
        check(!items.isEmpty() && items.first().id.size() == 11,
              "youtube videoId 11 字符",
              items.isEmpty() ? QString() : items.first().id);
        const QString cont = youtube::parse::continuation(r.p);
        check(!cont.isEmpty(), "youtube continuation 非空",
              QString::number(cont.size()));
    }
    {
        JRoot r(load(QString::fromUtf8("youtube_playlist.json")));
        const QList<QString> ids = youtube::parse::playlistVideoIds(r.p, 100);
        check(ids.size() >= 2, "youtube playlist ids>=2",
              QString::number(ids.size()));
    }

    // ── douyin ─────────────────────────────────────────────────
    {
        JRoot r(load(QString::fromUtf8("douyin_hot.json")));
        const QList<VideoItem> items = douyin::parse::hotWords(r.p);
        check(items.size() >= 3, "douyin hot words>=3",
              QString::number(items.size()));
        check(!items.isEmpty() && items.first().id.startsWith(QString::fromUtf8("hot:")),
              "douyin id=hot:<group_id>");
        check(!items.isEmpty()
                  && items.first().extra.value(QString::fromUtf8("kind")).toString()
                         == QString::fromUtf8("hotword"),
              "douyin extra.kind=hotword");
    }

    std::printf("parse: %s\n", g_fail ? "FAILED" : "all green");
    return g_fail ? 1 : 0;
}
