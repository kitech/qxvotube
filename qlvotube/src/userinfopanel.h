#ifndef USERINFOPANEL_H
#define USERINFOPANEL_H

#include "compat34.h"
#include <qwidget.h>

/* 用户信息区（两行式，同 SideNav 主题取色，自绘）。
 *   行1：头像（identicon）+ 昵称（未登录时 textMuted"未登录"）
 *   行2：副信息（未登录"点击登录"；登录后由 Phase 2 填充）
 * 整块可点击 → loginRequested()。
 * Phase 1 挂 SideNav 左上（搜索框上方）；Phase 2 登录后经 setLoggedIn 更新。 */
class UserInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit UserInfoPanel(QWidget* parent = 0);

    /* Phase 2 登录成功后调用（头像 seed 经 Identicon 绘制） */
    void setLoggedIn(const QString& name, const QString& avatarSeed);

signals:
    void loginRequested();

protected:
    void paintEvent(QPaintEvent* event);
    void mousePressEvent(QMouseEvent* event);

private:
    bool m_loggedIn;
    QString m_userName;
    QString m_avatarSeed;
};

#endif /* USERINFOPANEL_H */
