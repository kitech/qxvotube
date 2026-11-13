#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include "page.h"

class QskComboBox;
class QskSwitchButton;

// 设置页(自 qsktox 裁剪:去掉 phone/push/gotify/mozilla 等业务行):
//   页面切换动画 / 主题 / 明暗 / 字号 / 播放引擎(重启生效) / 调试背景
class SettingsPage : public Page
{
public:
    explicit SettingsPage(QQuickItem* parent = nullptr);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    QskComboBox* m_transitionCombo = nullptr;
    QskComboBox* m_skinCombo = nullptr;
    QskSwitchButton* m_darkSwitch = nullptr;
    QskComboBox* m_fontScaleCombo = nullptr;
    QskComboBox* m_engineCombo = nullptr;
    QskSwitchButton* m_debugBgSwitch = nullptr;
    int m_currentAnimatorIdx = -1;
    bool m_signalsConnected = false;
};

#endif
