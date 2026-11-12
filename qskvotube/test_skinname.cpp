// qskvotube 单测:皮肤名回归守卫(2026-11 文字/按钮不渲染根因)。
// 根因记录:QskSkinManager 皮肤名大小写敏感(Fluent2/Fusion/Material3),
// 传错静默回退第一个可用皮肤且无警告;另 styleHints 暗色主题会把
// DarkScheme 配色画在未填充的白窗底上——修复采用精确名+显式 LightScheme。
// 运行需 offscreen 平台(见 build_tests.sh)。
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <QGuiApplication>
#include <QskSkinManager.h>
#include <QskSkin.h>

static int g_argc = 0;
static char** g_argv = 0;

int main(int argc, char** argv)
{
    g_argc = argc;
    g_argv = argv;
    // offscreen 平台(脚本设 QT_QPA_PLATFORM=offscreen);整个进程一个 app
    QGuiApplication app(g_argc, g_argv);
    doctest::Context ctx;
    return ctx.run();
}

TEST_CASE("QskSkinManager - 皮肤名精确匹配") {
    qskSkinManager->setPluginPaths(QStringList()
        << "/opt/qt/qskinny/lib/qskinny/plugins");

    SUBCASE("精确名 Fusion - 加载成功") {
        qskSkinManager->setSkin("Fusion");
        CHECK(qskSkinManager->skinName() == QString("Fusion"));
    }
    SUBCASE("精确名 Material3 / Fluent2 - 加载成功") {
        qskSkinManager->setSkin("Material3");
        CHECK(qskSkinManager->skinName() == QString("Material3"));
        qskSkinManager->setSkin("Fluent2");
        CHECK(qskSkinManager->skinName() == QString("Fluent2"));
    }
    SUBCASE("小写 fusion - 静默回退(守卫:记录现有回退行为)") {
        qskSkinManager->setSkin("fusion");
        // 名字查找失败 → 回退 names.first()=Fluent2,绝不等于请求名
        CHECK(qskSkinManager->skinName() != QString("fusion"));
        CHECK(qskSkinManager->skinName() == QString("Fluent2"));
    }
    SUBCASE("皮肤对象非空且可设配色(白底应用须显式 LightScheme)") {
        qskSkinManager->setSkin("Fusion");
        QskSkin* skin = qskSkinManager->skin();
        REQUIRE(skin != 0);
        skin->setColorScheme(QskSkin::LightScheme);
        CHECK(skin->colorScheme() == QskSkin::LightScheme);
    }
    SUBCASE("可用皮肤名单含三件套") {
        const QStringList names = qskSkinManager->skinNames();
        CHECK(names.contains("Fusion"));
        CHECK(names.contains("Material3"));
        CHECK(names.contains("Fluent2"));
    }
}
