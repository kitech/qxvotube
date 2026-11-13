#include "settingspage.h"

#include <QSettings>

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

namespace {

// 字号档位 → 基准 pt(设置页下拉;Small/Medium/Large/XL)
const int kFontScales[] = { 12, 16, 20, 24 };

} // namespace

SettingsPage::SettingsPage(QQuickItem* parent)
    : Page(parent)
{
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
    auto* title = new QskTextLabel(QString::fromUtf8("设置"), topBar);
    title->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);
    title->setAlignment(Qt::AlignCenter);
    connect(backBtn, &QskAbstractButton::clicked, this, [this]() { finish(); });

    layout->addSpacer(16, 0);

    // ── 行1:页面切换动画 ──
    auto* row1 = new QskLinearBox(Qt::Horizontal, layout);
    row1->setSpacing(12);
    auto* tLabel = new QskTextLabel("页面切换动画", row1);
    tLabel->setPreferredWidth(160);
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
    auto* skinLabel = new QskTextLabel("主题", row2);
    skinLabel->setPreferredWidth(160);
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
    auto* schemeLabel = new QskTextLabel("颜色方案", row3);
    schemeLabel->setPreferredWidth(160);
    m_darkSwitch = new QskSwitchButton(row3);
    auto* schemeVal = new QskTextLabel("Light", row3);
    schemeVal->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Preferred);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行4:字号 ──
    auto* row4 = new QskLinearBox(Qt::Horizontal, layout);
    row4->setSpacing(12);
    auto* fontLabel = new QskTextLabel("字号", row4);
    fontLabel->setPreferredWidth(160);
    m_fontScaleCombo = new QskComboBox(row4);
    m_fontScaleCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_fontScaleCombo->addOption(QskLabelData("小"));
    m_fontScaleCombo->addOption(QskLabelData("中"));
    m_fontScaleCombo->addOption(QskLabelData("大"));
    m_fontScaleCombo->addOption(QskLabelData("特大"));
    m_fontScaleCombo->setCurrentIndex(1);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行5:播放引擎(重启生效)──
    auto* row5 = new QskLinearBox(Qt::Horizontal, layout);
    row5->setSpacing(12);
    auto* engineLabel = new QskTextLabel("播放引擎(重启生效)", row5);
    engineLabel->setPreferredWidth(160);
    m_engineCombo = new QskComboBox(row5);
    m_engineCombo->setSizePolicy(QskSizePolicy::Expanding, QskSizePolicy::Fixed);
    m_engineCombo->addOption(QskLabelData("ffmpeg"));
    m_engineCombo->addOption(QskLabelData("mpv"));
    m_engineCombo->setCurrentIndex(0);

    new QskSeparator(Qt::Horizontal, layout);

    // ── 行6:调试背景 ──
    auto* row6 = new QskLinearBox(Qt::Horizontal, layout);
    row6->setSpacing(12);
    auto* debugLabel = new QskTextLabel("调试背景", row6);
    debugLabel->setPreferredWidth(160);
    m_debugBgSwitch = new QskSwitchButton(row6);

    layout->addStretch(1);

    // ── 恢复持久化(先恢复再连信号,避免恢复触发写入/副作用)──
    QSettings s;
    const int transIdx = s.value("transition", 3).toInt();
    m_currentAnimatorIdx = transIdx;
    m_transitionCombo->setCurrentIndex(transIdx);
    m_skinCombo->setCurrentIndex(s.value("skin", 0).toInt());
    m_darkSwitch->setChecked(s.value("darkMode", true).toBool());   // 缺省深色
    m_fontScaleCombo->setCurrentIndex(s.value("fontScale", 1).toInt());
    m_engineCombo->setCurrentIndex(
        s.value("engine", "ffmpeg").toString() == QLatin1String("mpv") ? 1 : 0);
    m_debugBgSwitch->setChecked(s.value("debugBackground", false).toBool());
    schemeVal->setText(m_darkSwitch->isChecked() ? "Dark" : "Light");
    if (m_debugBgSwitch->isChecked()) {
        QskSetup::setUpdateFlag(QskItem::DebugForceBackground, true);
    }

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
            schemeVal->setText(checked ? "Dark" : "Light");
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

    connect(m_debugBgSwitch, &QskAbstractButton::toggled,
        this, [](bool checked) {
            QSettings().setValue("debugBackground", checked);
            QskSetup::setUpdateFlag(QskItem::DebugForceBackground, checked);
        });
}
