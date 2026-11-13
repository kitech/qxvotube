#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include "page.h"

#include <vector>

class QskComboBox;
class QskSwitchButton;
class QskTextLabel;

// 设置页(自 qsktox 裁剪:去掉 phone/push/gotify/mozilla 等业务行):
//   页面切换动画 / 主题 / 明暗 / 字号 / 播放引擎(重启生效) / 调试背景 / 语言
// 缺省值按运行平台/系统环境判定(cxDefaultLang / cxDefaultFontScale)。
class SettingsPage : public Page
{
public:
    explicit SettingsPage(QQuickItem* parent = nullptr);

    // 平台/系统环境判定(公开供测试与其它页复用)
    static QString cxDefaultLang();        // 系统 locale → zh-CN/zh-TW/en-US
    static int cxDefaultFontScaleIndex();  // Android 中(1) / 桌面 小(0)

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;

private:
    struct TrItem { QskTextLabel* label; const char* key; };

    void retranslate();
    void applyTransition(int idx);   // 切转场动画(即时生效,不必重启)

    QskComboBox* m_transitionCombo = nullptr;
    QskComboBox* m_skinCombo = nullptr;
    QskSwitchButton* m_darkSwitch = nullptr;
    QskComboBox* m_fontScaleCombo = nullptr;
    QskComboBox* m_engineCombo = nullptr;
    QskSwitchButton* m_debugBgSwitch = nullptr;
    QskComboBox* m_langCombo = nullptr;
    int m_currentAnimatorIdx = -1;
    bool m_signalsConnected = false;
    std::vector<TrItem> m_tr;
};

#endif
