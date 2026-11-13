#ifndef MAINPAGE_H
#define MAINPAGE_H

#include "page.h"

#include <QSettings>
#include <QStringList>
#include <functional>

class CXVideoPlayer;
class CXQuickVideoItem;
class CXQuickMpvFboItem;
class CxDropHook;
class CxUserAvatar;
class QskLinearBox;
class QskPushButton;
class QskSlider;
class QskTextLabel;

// 主页:原 main.cpp 的 UI + 播放器接线整体搬入(行为不变);
// 新增 TopBar(标题 + ⋮ 菜单:保持屏幕常亮/设置/关于)
class MainPage : public Page
{
    Q_OBJECT
public:
    explicit MainPage(QQuickItem* parent = nullptr);
    ~MainPage() override;

    void showEngineError(const QString& text);
    void stopPlayback();   // 退出前停引擎(消渲染线程退出竞态)
    // 用户区(qskmusic 同语义;登录能力落地前无调用方,仅区分已登录/未登录)
    void setLoggedIn(const QString& nickname, const QString& avatarSeed);

protected:
    void onCreate(const QVariantMap& launchArgs,
                  const QVariantMap& savedState) override;
    void onResume() override;

private:
    void showOptionsMenu(const QPointF& origin);
    void applyKeepScreenOn(bool on);
    void openPl(int idx);           // 切歌(回绕/播完停)
    void buildControls(QskLinearBox* box);

    CXVideoPlayer* m_player = nullptr;
    CXQuickVideoItem* m_ffItem = nullptr;
    CXQuickMpvFboItem* m_mpvItem = nullptr;
    CxDropHook* m_dropHook = nullptr;

    QskLinearBox* m_box = nullptr;
    QskPushButton* m_playBtn = nullptr;
    QskSlider* m_prog = nullptr;
    QskTextLabel* m_timeLbl = nullptr;
    QskTextLabel* m_subLabel = nullptr;
    CxUserAvatar* m_userAvatar = nullptr;
    QskTextLabel* m_userName = nullptr;
    QskTextLabel* m_userStatus = nullptr;

    QStringList m_pl;
    int m_plIdx = 0;
    int m_rateIdx = 0;
    int m_subIdx = -1;
    int m_eqIdx = 0;
    long m_durMs = 0;
    bool m_muted = false;
    bool m_dbg = false;
    bool m_wantMpv = false;
    bool m_keepScreenOn = true;
    QSettings* m_plSet = nullptr;

    const double m_rates[4] = { 1.0, 1.25, 1.5, 2.0 };
};

#endif
