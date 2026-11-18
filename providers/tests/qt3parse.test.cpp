// Qt3 离线解析/签名验证(fixtures 回放,不联网):
//   ./qt3parse ../fixtures
// 覆盖:douyin awemeDetail/streamsOf/searchList + acsig golden
//       + youtube guideItems/browseItems(三种 renderer)
#include "douyin/parse.h"
#include "douyin/acsig.h"
#include "youtube/parse.h"
#include "common/qcjson.h"

#include <qapplication.h>
#include <qfile.h>
#include <qstring.h>
#include <stdio.h>

using namespace qxv;

namespace {

int g_fail = 0;

void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qToUtf8(got).data()); }
}

QByteArray load(const QString& dir, const QString& name) {
    QFile f(dir + qFromUtf8("/") + name);
    if (!qOpenReadOnly(f)) {
        std::printf("[FAIL] fixture missing: %s\n", qToUtf8(name).data());
        ++g_fail;
        return QByteArray();
    }
    return f.readAll();
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString dir = argc > 1 ? QString::fromUtf8(argv[1])
                                 : qFromUtf8("../fixtures");

    // ── __ac_signature golden ───────────────────────────────────
    {
        const QString sig = douyin::acSignature(
            1740635781, qFromUtf8("www.douyin.com/"),
            qFromUtf8("067bffadf00143f576ddf"),
            qFromUtf8("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                      "AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/133.0.0.0 Safari/537.36 Edg/133.0.0.0"));
        check(sig == qFromUtf8("_02B4Z6wo00f01HBw-fgAAIDA-rdLmXfuMxxwUP1AAHurb5"),
              "acSignature golden", sig);
    }

    // ── douyin detail / streams / search ────────────────────────
    {
        JRoot r(load(dir, qFromUtf8("douyin_detail.json")));
        VideoItem vi;
        QString err;
        check(douyin::parse::awemeDetail(r.p, &vi, &err),
              "awemeDetail ok", err);
        check(vi.publishMs == 1704795919000LL, "publishMs=create_time*1000",
              QString::number(vi.publishMs));
        check(vi.durationMs == 868566, "durationMs", QString::number(vi.durationMs));
        check(!vi.author.isEmpty() && !vi.title.isEmpty(), "author/title 非空");

        const QList36<Stream> ss = douyin::parse::streamsOf(r.p);
        check(ss.size() == 6, "streams 6 档", QString::number(ss.size()));
        long long prev = -1;
        bool asc = true;
        QStringList gears;
        for (int i = 0; i < ss.size(); ++i) {
            const long long b =
                ss[i].extra[qFromUtf8("bitRate")].toLongLong();
            if (b < prev) asc = false;
            prev = b;
            gears.append(ss[i].quality);
        }
        check(asc, "bitRate 升序");
        check(gears.contains(qFromUtf8("1080P"))
                  && gears.contains(qFromUtf8("720P")),
              "gear_name→1080P/720P", gears.join(qFromUtf8(",")));
    }
    {
        JRoot r(load(dir, qFromUtf8("douyin_search.json")));
        const QList36<VideoItem> items = douyin::parse::searchList(r.p);
        check(items.size() == 2, "searchList 跳过非视频卡",
              QString::number(items.size()));
    }

    // ── youtube guide / browse ──────────────────────────────────
    {
        JRoot r(load(dir, qFromUtf8("yt_guide.json")));
        const QList36<CategoryItem> cats = youtube::parse::guideItems(r.p);
        check(cats.size() >= 3, "guide UC* 分类>=3",
              QString::number(cats.size()));
    }
    {
        JRoot r(load(dir, qFromUtf8("yt_browse_music.json")));
        const QList36<VideoItem> items = youtube::parse::browseItems(r.p);
        check(items.size() == 2, "music VIDEO lockup==2", 
              QString::number(items.size()));
        check(!items.isEmpty() && items.first().id.length() == 11,
              "music videoId 11 字符");
    }
    {
        JRoot r(load(dir, qFromUtf8("yt_browse_gaming.json")));
        const QList36<VideoItem> items = youtube::parse::browseItems(r.p);
        check(items.size() == 2, "gaming videoCard==2",
              QString::number(items.size()));
    }
    {
        JRoot r(load(dir, qFromUtf8("yt_browse_live.json")));
        const QList36<VideoItem> items = youtube::parse::browseItems(r.p);
        check(items.size() == 2, "live videoRenderer==2",
              QString::number(items.size()));
    }

    std::printf(g_fail == 0 ? "qt3parse: all green\n" : "qt3parse: %d FAIL\n",
                g_fail);
    return g_fail == 0 ? 0 : 1;
}
