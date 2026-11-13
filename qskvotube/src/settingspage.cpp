#include "settingspage.h"

#include <QSettings>
#include <QLocale>

#include <QskLinearBox.h>
#include <QskTextLabel.h>
#include <QskPushButton.h>
#include <QskComboBox.h>
#include <QskSwitchButton.h>
#include <QskLabelData.h>
#include <QskSeparator.h>
#include <QskStackBox.h>
#include <QskStackBoxAnimator.h>
#include <QskSetup.h>
#include <QskItem.h>
#include <QskSkinManager.h>
#include <QskSkin.h>

#include "pagemanager.h"
#include "uistyle.h"
#include "translator.h"

namespace {

// 字号档位 → 基准 pt(设置页下拉;Small/Medium/Large/XL)
const int kFontScales[] = { 12, 16, 20, 24 };

// 语言下拉:code / 显示名 key
const char* const kLangCodes[] = { "zh-CN", "zh-TW", "en-US" };
const char* const kLangKeys[]  = { "lang.zh_CN", "lang.zh_TW", "lang.en_US" };

QString tr(const char* key)
{
    return Translator::instance().t(QString::fromLatin1(key));
}

} // namespace

QString SettingsPage::cxDefaultLang()
{
    // 运行环境判定:持久化优先(由调用方处理),这里给系统 locale 缺省
    const QString name = QLocale::system().name();   // e.g. zh_CN / zh_TW / en_US
    if (name.startsWith("zh_TW") || name.startsWith("zh_HK")
        || name.startsWith("zh_Hant"))
        return "zh-TW";
    if (name.startsWith("zh"))
        return "zh-CN";
    return "en-US";
}

int SettingsPage::cxDefaultFontScaleIndex()
{
    // Android 触摸屏默认中号(16pt);桌面默认小号(12pt 档,近原 10pt 观感)
#ifdef Q_OS_ANDROID
    return 1;
#else
    return 0;
#endif
}

SettingsPage::SettingsPage(QQuickItem* parent)
    : Page(parent)
{
}

void SettingsPage::retranslate()
{
    for (const TrItem& it : m_tr) {
        if (it.label != nullptr)
            it.label->setText(tr(it.key));
    }
    if (m_langCombo != nullptr) {
        QVector<QskLabelData> opts;
        for (int i = 0; i < 3; ++i)
            opts.append(QskLabelData(tr(kLangKeys[i])));
        m_langCombo->setOptions(opts);
    }
}

void SettingsPage::onCreate(const QVariantMap&, const QVariantMap&)
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
    auto* title = new QskTextLabel(tr("settings.title"), topBar);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    title->setAlignment(Qt::AlignCenter);
    m_tr.push_back({ title, "settings.title" });
    connect(backBtn, &QskAbstractButton::clicked, this, [this]() { finish(); });

    layout->addSpacer(16, 0);

    auto addLabel = [this](QskLinearBox* row, const char* key) {
        auto* l = new QskTextLabel(tr(key), row);
        l->setPreferredWidth(160);
        m_tr.push_back({ l, key });
        return l;
    };

    // ── 行1:页面切换动画 ──
    auto* row1 = new QskLinearBox(Qt::Horizontal, layout);
    row1->setSpacing(12);
    addLabel(row1, "settings.transition");
    m_transitionCombo = new QskComboBox(row1);
    m_transitionCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_transitionCombo->addOption(QskLabelData("Slide"));
    m_transitionCombo->addOption(QskLabelData("2D"));
    m_transitionCombo->addOption(QskLabelData("3D"));
    m_transitionCombo->addOption(QskLabelData("Perspective"));
    m_transitionCombo->setCurrentIndex(3);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行2:主题 ──
    auto* row2 = new QskLinearBox(Qt::Horizontal, layout);
    row2->setSpacing(12);
    addLabel(row2, "settings.theme");
    m_skinCombo = new QskComboBox(row2);
    m_skinCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_skinCombo->addOption(QskLabelData("Fusion"));
    m_skinCombo->addOption(QskLabelData("Fluent2"));
    m_skinCombo->addOption(QskLabelData("Material3"));
    m_skinCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行3:明暗 ──
    auto* row3 = new QskLinearBox(Qt::Horizontal, layout);
    row3->setSpacing(12);
    addLabel(row3, "settings.scheme");
    m_darkSwitch = new QskSwitchButton(row3);
    auto* schemeVal = new QskTextLabel(QString(), row3);
    schemeVal->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行4:字号 ──
    auto* row4 = new QskLinearBox(Qt::Horizontal, layout);
    row4->setSpacing(12);
    addLabel(row4, "settings.font_size");
    m_fontScaleCombo = new QskComboBox(row4);
    m_fontScaleCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_fontScaleCombo->addOption(QskLabelData(tr("settings.size_s")));
    m_fontScaleCombo->addOption(QskLabelData(tr("settings.size_m")));
    m_fontScaleCombo->addOption(QskLabelData(tr("settings.size_l")));
    m_fontScaleCombo->addOption(QskLabelData(tr("settings.size_xl")));

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行5:播放引擎(重启生效)──
    auto* row5 = new QskLinearBox(Qt::Horizontal, layout);
    row5->setSpacing(12);
    addLabel(row5, "settings.engine");
    m_engineCombo = new QskComboBox(row5);
    m_engineCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_engineCombo->addOption(QskLabelData("ffmpeg"));
    m_engineCombo->addOption(QskLabelData("mpv"));

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行6:语言 ──
    auto* row6 = new QskLinearBox(Qt::Horizontal, layout);
    row6->setSpacing(12);
    addLabel(row6, "settings.language");
    m_langCombo = new QskComboBox(row6);
    m_langCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    for (int i = 0; i < 3; ++i)
        m_langCombo->addOption(QskLabelData(tr(kLangKeys[i])));

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行7:调试背景 ──
    auto* row7 = new QskLinearBox(Qt::Horizontal, layout);
    row7->setSpacing(12);
    addLabel(row7, "settings.debug_bg");
    m_debugBgSwitch = new QskSwitchButton(row7);

    layout->addStretch(1);

    // ── 恢复持久化:无键时按运行平台/系统环境取缺省(先恢复再连信号)──
    QSettings s;
    const int transIdx = s.value("transition", 3).toInt();
    m_currentAnimatorIdx = transIdx;
    m_transitionCombo->setCurrentIndex(transIdx);
    m_skinCombo->setCurrentIndex(s.value("skin", 0).toInt());
    m_darkSwitch->setChecked(s.value("darkMode", true).toBool());   // 缺省深色
    m_fontScaleCombo->setCurrentIndex(
        s.value("fontScale", cxDefaultFontScaleIndex()).toInt());
    m_engineCombo->setCurrentIndex(
        s.value("engine", "ffmpeg").toString() == QLatin1String("mpv") ? 1 : 0);
    m_debugBgSwitch->setChecked(s.value("debugBackground", false).toBool());

    const QString savedLang = s.value("lang", cxDefaultLang()).toString();
    int langIdx = 0;
    for (int i = 0; i < 3; ++i)
        if (savedLang == QLatin1String(kLangCodes[i]))
            langIdx = i;
    m_langCombo->setCurrentIndex(langIdx);

    schemeVal->setText(m_darkSwitch->isChecked() ? tr("settings.dark")
                                                 : tr("settings.light"));
    if (m_debugBgSwitch->isChecked())
        QskSetup::setUpdateFlag(QskItem::DebugForceBackground, true);

    // 首次写入缺省值(便于其它模块读到同一口径)
    if (!s.contains("lang"))
        s.setValue("lang", kLangCodes[langIdx]);
    if (!s.contains("fontScale"))
        s.setValue("fontScale", m_fontScaleCombo->currentIndex());

    // 语言热切:本页重译(其它页各自 connected)
    connect(&Translator::instance(), &Translator::languageChanged,
        this, [this, schemeVal]() {
            retranslate();
            // 字号 4 档选项与明暗值也随语言刷新
            QVector<QskLabelData> sizeOpts;
            sizeOpts.append(QskLabelData(tr("settings.size_s")));
            sizeOpts.append(QskLabelData(tr("settings.size_m")));
            sizeOpts.append(QskLabelData(tr("settings.size_l")));
            sizeOpts.append(QskLabelData(tr("settings.size_xl")));
            const int cur = m_fontScaleCombo->currentIndex();
            m_fontScaleCombo->blockSignals(true);
            m_fontScaleCombo->setOptions(sizeOpts);
            m_fontScaleCombo->setCurrentIndex(cur);
            m_fontScaleCombo->blockSignals(false);
            schemeVal->setText(m_darkSwitch->isChecked()
                               ? tr("settings.dark") : tr("settings.light"));
        });

    if (m_signalsConnected)
        return;
    m_signalsConnected = true;

    // ── 切动画:同类型不重建(qsktox 同款去重)──
    connect(m_transitionCombo, &QskComboBox::currentIndexChanged,
        this, [this](int index) {
            if (index == m_currentAnimatorIdx)
                return;
            m_currentAnimatorIdx = index;
            QSettings().setValue("transition", index);
            auto* sb = pageManager() ? pageManager()->stackBox() : nullptr;
            if (!sb)
                return;
            QskStackBoxAnimator* a = nullptr;
            switch (index) {
                case 0: a = new QskStackBoxAnimator1(sb); break;
                case 1: a = new QskStackBoxAnimator2(sb); break;
                case 2: a = new QskStackBoxAnimator3(sb); break;
                default: a = new QskStackBoxAnimator4(sb); break;
            }
            if (a)
                sb->setAnimator(a);
        });

    connect(m_skinCombo, &QskComboBox::currentIndexChanged,
        this, [](int index) {
            QSettings().setValue("skin", index);
            static const char* names[] = { "Fusion", "Fluent2", "Material3" };
            if (index >= 0 && index < 3) {
                qskSkinManager->setSkin(names[index]);
                cxApplySkinTheme(qskSkinManager->skin());
            }
        });

    connect(m_darkSwitch, &QskAbstractButton::toggled,
        this, [schemeVal](bool checked) {
            schemeVal->setText(checked ? tr("settings.dark")
                                       : tr("settings.light"));
            cxSetDarkMode(checked);   // 持久化 + 应用(统一入口)
        });

    connect(m_fontScaleCombo, &QskComboBox::currentIndexChanged,
        this, [](int index) {
            QSettings().setValue("fontScale", index);
            if (index >= 0 && index < 4)
                cxSetBasePointSize(kFontScales[index]);
        });

    connect(m_engineCombo, &QskComboBox::currentIndexChanged,
        this, [](int index) {
            // 引擎在窗口创建前确定,运行期不可热换 → 重启生效
            QSettings().setValue("engine",
                index == 1 ? QString("mpv") : QString("ffmpeg"));
        });

    connect(m_langCombo, &QskComboBox::currentIndexChanged,
        this, [](int idx) {
            if (idx < 0 || idx > 2)
                return;
            const QString code = QString::fromLatin1(kLangCodes[idx]);
            QSettings().setValue("lang", code);
            Translator::instance().loadLanguage(code);   // → languageChanged
        });

    connect(m_debugBgSwitch, &QskAbstractButton::toggled,
        this, [](bool checked) {
            QSettings().setValue("debugBackground", checked);
            QskSetup::setUpdateFlag(QskItem::DebugForceBackground, checked);
        });
}
