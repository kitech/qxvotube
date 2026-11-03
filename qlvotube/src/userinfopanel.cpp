#include "userinfopanel.h"

#include "compat34.h"
#include "musicstyle.h"
#include "identicon.h"
#include "translator.h"

#include <qevent.h>
#include <qpainter.h>
#include <qfontmetrics.h>

namespace {
/* Qt3 无 QFontMetrics::elidedText：手动尾部截断 */
QString elideRight(const QFontMetrics& fm, const QString& text, int maxW)
{
    if (text.isEmpty() || qFontWidth(fm, text) <= maxW) return text;
    QString t = text;
    const QString dots = qFromUtf8("…");
    while (!t.isEmpty() && qFontWidth(fm, t + dots) > maxW)
        t.remove(t.length() - 1, 1);
    return t + dots;
}
}

UserInfoPanel::UserInfoPanel(QWidget* parent)
    : QWidget(parent), m_loggedIn(false)
{
    setMouseTracking(false);
}

void UserInfoPanel::setLoggedIn(const QString& name, const QString& avatarSeed)
{
    m_loggedIn = !name.isEmpty();
    m_userName = name;
    m_avatarSeed = avatarSeed.isEmpty() ? name : avatarSeed;
    update();
}

void UserInfoPanel::paintEvent(QPaintEvent* event)
{
    (void)event;
    QPainter p(this);
    const StyleParams::Palette& pal = muiPalette();

    p.setPen(Qt::NoPen);
    p.fillRect(rect(), pal.baseBg);

    QFont normal = this->font();
    p.setFont(normal);
    QFontMetrics fm(normal);

    /* 预留头像区域：36px 区块；未登录也有 identicon 占位，不空缺 */
    QString seed = m_loggedIn ? m_avatarSeed : QString::fromUtf8("anonymous");
    QPixmap avatar = generateIdenticon(seed, 36);
    QRect av(10, 11, 36, 36);
    p.drawPixmap(av, avatar);

    const int textX = av.right() + 10;
    const int textW = width() - textX - 8;

    /* 行1：昵称（未登录 = "未登录" muted），与头像上沿对齐 */
    QString name = m_loggedIn ? m_userName : _("user.not_login");
    p.setPen(m_loggedIn ? pal.textPrimary : pal.textMuted);
    p.drawText(QRect(textX, 12, textW, 16), Qt::AlignVCenter | Qt::AlignLeft,
               elideRight(fm, name, textW));

    /* 行2：副信息（未登录"点击登录"；登录后 Phase 2 内容） */
    QString sub = m_loggedIn ? _("user.logged_in")
                             : _("user.click_to_login");
    QFont small = normal;
    small.setPointSize(qMax(7, small.pointSize() - 2));
    p.setFont(small);
    QFontMetrics fm2(small);
    p.setPen(pal.textMuted);
    p.drawText(QRect(textX, 31, textW, 13), Qt::AlignVCenter | Qt::AlignLeft,
               elideRight(fm2, sub, textW));

    /* 底部分割线（与搜索框/列表区隔开） */
    p.setPen(pal.border);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void UserInfoPanel::mousePressEvent(QMouseEvent* event)
{
    (void)event;
    emit loginRequested();
}
