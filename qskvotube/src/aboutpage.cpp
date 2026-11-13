#include "aboutpage.h"
#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskSeparator.h>
#include <QskFontRole.h>
#include <QSysInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include "version_config.h"
#include "translator.h"

namespace {
// 词表取值(qlcomp Translator);About 为 Transient 页,每次打开重建即用当前语言
QString trKey(const char* key)
{
    return Translator::instance().t(QString::fromLatin1(key));
}
} // namespace

AboutPage::AboutPage(QQuickItem* parent)
    : Page(parent)
{
}

void AboutPage::onCreate(const QVariantMap&, const QVariantMap&)
{
    setAutoLayoutChildren(true);
    auto* layout = new QskLinearBox(Qt::Vertical, this);
    layout->setPanel(true);

    // ── TopBar ──
    auto* topBar = new QskLinearBox(Qt::Horizontal, layout);
    topBar->setPanel(true);
    topBar->setPreferredHeight(56);

    auto* backBtn = new QskPushButton(QString::fromUtf8("←"), topBar);
    backBtn->setPreferredSize(44, 44);
    auto* title = new QskTextLabel(trKey("about.title"), topBar);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    title->setAlignment(Qt::AlignCenter);

    connect(backBtn, &QskAbstractButton::clicked, this, [this]() {
        finish();
    });

    // Helper: section header
    auto addSection = [layout](const QString& heading) {
        layout->addSpacer(16, 0);
        auto* h = new QskTextLabel(heading, layout);
        h->setFontRole(QskFontRole(QskFontRole::Title, QskFontRole::Normal));
        new QskSeparator(Qt::Horizontal, layout);
    };

    // Helper: labeled value row
    auto addRow = [layout](const QString& label, const QString& value) {
        auto* row = new QskLinearBox(Qt::Horizontal, layout);
        row->setPreferredHeight(36);
        auto* lbl = new QskTextLabel(label, row);
        lbl->setPreferredWidth(120);
        auto* val = new QskTextLabel(value, row);
        val->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };

    // ── Section: App ──
    addSection(trKey("about.app"));
    addRow(trKey("about.name"), "qskvotube");
    addRow(trKey("about.version"), QString(APP_VERSION_NAME " (%1)").arg(APP_VERSION_CODE));
    addRow(trKey("about.qt"), QT_VERSION_STR);
    addRow(trKey("about.rhi"), QQuickWindow::sceneGraphBackend());
#ifdef Q_PROCESSOR_ARM_V8
    addRow(trKey("about.arch"), "arm64-v8a");
#else
    addRow(trKey("about.arch"), "unknown");
#endif

    // ── Section: Device ──
    addSection(trKey("about.device"));

    auto* screen = QGuiApplication::primaryScreen();
    QString screenInfo = QString("%1x%2 @%3x")
        .arg(screen->size().width()).arg(screen->size().height())
        .arg(screen->devicePixelRatio(), 0, 'f', 2);

    addRow(trKey("about.brand"), QSysInfo::productType());
    addRow(trKey("about.model"), QSysInfo::prettyProductName());
#if defined(QT_ANDROID_EXAMPLES) || defined(Q_OS_ANDROID)
    addRow(trKey("about.android"), QString("%1 (API %2)")
        .arg(QSysInfo::productVersion())
        .arg(QSysInfo::buildAbi().split('-').first().toInt()));
#endif
    addRow(trKey("about.screen"), screenInfo);
    addRow(trKey("about.dpi"), QString::number(screen->logicalDotsPerInch(), 'f', 0));
    addRow(trKey("about.dpr"), QString::number(screen->devicePixelRatio(), 'f', 2));
    addRow(trKey("about.kernel"), QSysInfo::kernelType() + " " + QSysInfo::kernelVersion());

    layout->addStretch(1);
}
