#include "videopane.h"
#include "qlcompat34.h"
#include <qpushbutton.h>
#include <qcolor.h>
#include <qevent.h>
#include <qtimer.h>
#include <qapplication.h>

VideoPane::VideoPane(QWidget* parent)
    : QWidget(parent), m_surface(0), m_renderer(0), m_lay(0),
      m_backBtn(0), m_engineLabel(0), m_titleLabel(0), m_subLabel(0),
      m_clickTimer(0)
{
#ifdef QT3_BUILD
    setBackgroundColor(QColor(0, 0, 0)); // Qt3 无 palette/autoFill
#else
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0, 0, 0));
    setPalette(pal);
#endif

    m_lay = qNewBoxLayout(this, QBoxLayout::TopToBottom, 0, 0);

    // 顶条:返回列表 + 引擎名(极简,避免与 cxvideo 字体/暗色主题打架)
    QWidget* bar = new QWidget(this);
    QBoxLayout* barLay = qNewBoxLayout(bar, QBoxLayout::LeftToRight, 4, 4);
    m_backBtn = new QPushButton(qFromUtf8("<"), bar);
    m_backBtn->setFixedWidth(32);
    m_engineLabel = new QLabel(bar);
    m_titleLabel = new QLabel(bar);
    m_titleLabel->setText("");
    barLay->addWidget(m_backBtn, 0);
    barLay->addWidget(m_titleLabel, 1);   /* 视频标题:占中间,过长自动省略 */
    barLay->addWidget(m_engineLabel, 0);  /* 引擎名:靠右,紧挨分辨率/档位 */
    /* M7:右侧实时分辨率/档位 */
    m_infoLabel = new QLabel(bar);
    m_infoLabel->setText("");
#ifdef QT3_BUILD
    {
        QPalette ip = m_infoLabel->palette();
        ip.setColor(QPalette::Active, QColorGroup::Foreground, QColor(170, 170, 170));
        ip.setColor(QPalette::Inactive, QColorGroup::Foreground, QColor(170, 170, 170));
        m_infoLabel->setPalette(ip);
    }
#else
    m_infoLabel->setStyleSheet(QString::fromLatin1("color: #aaaaaa;"));
#endif
    barLay->addWidget(m_infoLabel, 0);
    m_lay->addWidget(bar, 0);

    m_surface = new QWidget(this);
#ifdef QT3_BUILD
    m_surface->setBackgroundColor(QColor(0, 0, 0));
#else
    m_surface->setAutoFillBackground(true);
    QPalette sp = m_surface->palette();
    sp.setColor(QPalette::Window, QColor(0, 0, 0));
    m_surface->setPalette(sp);
#endif
    m_lay->addWidget(m_surface, 1);

    // 原生子件(macOS Qt4 WA_NativeWindow)在布局里可能不被撑开;
    // 监听 surface 的真实 resize,强制同步 renderer 几何(版本无关)。
    m_surface->installEventFilter(this);

    // 字幕叠加(位于 surface 底部;透明底 + 白字 + 黑描边效果靠背景半透明)
    m_subLabel = new QLabel(m_surface);
    m_subLabel->setText("");
#ifdef QT3_BUILD
    m_subLabel->setBackgroundColor(QColor(0, 0, 0));
    QPalette lp = m_subLabel->palette();
    lp.setColor(QPalette::Active, QColorGroup::Foreground, QColor(255, 255, 255));
    lp.setColor(QPalette::Inactive, QColorGroup::Foreground, QColor(255, 255, 255));
    m_subLabel->setPalette(lp);
#else
    m_subLabel->setStyleSheet(
        "background: rgba(0,0,0,140); color: white; padding: 2px 8px;");
#endif
    m_subLabel->setAlignment(Qt::AlignCenter);
    m_subLabel->hide();

    /* 画面单击/双击判定:按下启动,期间收到双击则取消 */
    m_clickTimer = new QTimer(this);
#ifndef QT3_BUILD
    m_clickTimer->setSingleShot(true);   /* Qt3 无此接口,start(msec,true) 传单次 */
#endif
    QObject::connect(m_clickTimer, SIGNAL(timeout()),
                     this, SLOT(onSurfaceClickTimeout()));

    QObject::connect(m_backBtn, SIGNAL(clicked()), this, SIGNAL(backRequested()));
}

void VideoPane::setRendererWidget(QWidget* w)
{
    if (w == 0)
        return;
#ifdef QT3_BUILD
    w->reparent(m_surface, QPoint(0, 0), true);
#else
    w->setParent(m_surface);
#endif
    m_renderer = w;
    w->installEventFilter(this);   /* 鼠标/滚轮事件落在渲染子件上,必须挂这里 */
    // 交给 m_surface 的布局管理：minimumSize(320,180) 与 surface 尺寸变化自动生效；
    // 不再用一次性 setGeometry（会绕过 min 且滞后于 surface 变化）
    QBoxLayout* rendererLay = qNewBoxLayout(m_surface, QBoxLayout::LeftToRight, 0, 0);
    rendererLay->addWidget(w, 1);
    // 首帧兜底:布局尚未稳定时先按 surface 当前尺寸对齐一次
    w->setGeometry(0, 0, m_surface->width(), m_surface->height());
    w->show();
    w->raise();
}

void VideoPane::setSubtitleText(const QString& text)
{
    if (m_subLabel == 0)
        return;
    if (text.isEmpty()) {
        m_subLabel->hide();
        return;
    }
    m_subLabel->setText(text);
    QSize hint = m_subLabel->sizeHint();
    int w = hint.width() < width() - 40 ? hint.width() : width() - 40;
    m_subLabel->setGeometry((m_surface->width() - w) / 2,
                            m_surface->height() - hint.height() - 16,
                            w, hint.height());
    m_subLabel->show();
    m_subLabel->raise();
}

void VideoPane::setEngineLabel(const QString& name)
{
    if (m_engineLabel != 0)
        m_engineLabel->setText(name);
}

void VideoPane::setVideoTitle(const QString& title)
{
    if (m_titleLabel != 0)
        m_titleLabel->setText(title);
}

void VideoPane::setVideoInfoText(const QString& text)
{
    if (m_infoLabel != 0)
        m_infoLabel->setText(text);
}

void VideoPane::contextMenuEvent(QContextMenuEvent* e)
{
    emit contextMenuRequested(e->globalPos());
    e->accept();
}

void VideoPane::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    // m_surface 由外层 m_lay 管理，renderer 由 m_surface 的布局管理；
    // 二者尺寸自动联动，不再手动 setGeometry（原实现高度沿用旧值会卡住）。
}

void VideoPane::onSurfaceClickTimeout()
{
    emit togglePlayPauseRequested();
}

bool VideoPane::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_surface || o == m_renderer) {
        const QEvent::Type t = e->type();
        if (t == QEvent::MouseButtonPress) {
            QMouseEvent* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                /* 按下先挂起:若随后是双击会被取消,否则超时=单击 */
                if (m_clickTimer != 0) {
#ifdef QT3_BUILD
                    m_clickTimer->start(QApplication::doubleClickInterval(), true);
#else
                    m_clickTimer->start(QApplication::doubleClickInterval());
#endif
                }
                return false;   /* 不改变原有按下行为 */
            }
#if QT_VERSION >= 0x050000
            if (me->button() == Qt::MiddleButton) {
#else
            if (me->button() == Qt::MidButton) {
#endif
                emit surfaceHint(HintMidClick);
                return true;
            }
        } else if (t == QEvent::MouseButtonDblClick) {
            QMouseEvent* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                if (m_clickTimer != 0)
                    m_clickTimer->stop();
                emit toggleFullscreenRequested();
                return true;
            }
        } else if (t == QEvent::Wheel) {
            QWheelEvent* we = static_cast<QWheelEvent*>(e);
#if QT_VERSION >= 0x060000
            const int d = we->angleDelta().y();
#else
            const int d = we->delta();
#endif
            emit surfaceHint(d < 0 ? HintWheelDown : HintWheelUp);
            return true;
        } else if (o == m_surface && t == QEvent::Resize && m_renderer != 0) {
            // 用事件自带的新尺寸,避开旧实现"沿用 m_surface->height()"的 stale 值
            QResizeEvent* re = static_cast<QResizeEvent*>(e);
            QSize s = re->size();
            m_renderer->setGeometry(0, 0, s.width(), s.height());
        }
    }
    return QWidget::eventFilter(o, e);
}
