// qskvotube —— QSkinny（Qsk*）前端占位骨架。
// 完整 UI 移植见 qlvotube/（Qt Widgets 版）；本工程为 QSkinny 变体的占位，
// 后续按 qskcomp/AGENTS.md 的组件规范逐步移植。
#include <QGuiApplication>

#include <QskSkinManager.h>
#include <QskWindow.h>
#include <QskTextLabel.h>
#include <QskLinearBox.h>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("qskvotube");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("qlvotube");

    for (const auto& a : app.arguments()) {
        if (a == "-v" || a == "--version") {
            printf("%s %s\n", "qskvotube", app.applicationVersion().toUtf8().constData());
            return 0;
        }
    }

    // 头less 环境无法加载图形皮肤插件，QskSkinManager 会断言；
    // 仅在可创建平台窗口时才进 UI。
    if (!QGuiApplication::platformName().startsWith("xcb") &&
        !QGuiApplication::platformName().startsWith("wayland")) {
        fprintf(stderr, "qskvotube: no GUI platform (%s), exiting\n",
            QGuiApplication::platformName().toUtf8().constData());
        return 1;
    }

    qskSkinManager->setSkin("materialDark");

    QskWindow window;
    window.setTitle("qskvotube");
    window.setPreferredSize({480, 320});

    auto* box = new QskLinearBox(Qt::Vertical);
    box->setMargins(24);
    box->setSpacing(12);

    auto* title = new QskTextLabel("qskvotube");
    box->addSpacer(20);
    box->addItem(title);

    auto* hint = new QskTextLabel("UI 移植进行中 — 完整 Widgets 版见 qlvotube/");
    box->addItem(hint);

    window.addItem(box);
    window.show();

    return app.exec();
}
