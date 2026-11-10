#include "videopane.h"
#include "compat34.h"
#include <qpushbutton.h>
#include <qcolor.h>
#include <qevent.h>

VideoPane::VideoPane(QWidget* parent)
    : QWidget(parent), m_surface(0), m_renderer(0), m_lay(0),
      m_backBtn(0), m_engineLabel(0)
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
    barLay->addWidget(m_backBtn, 0);
    barLay->addWidget(m_engineLabel, 1);
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
    w->setGeometry(m_surface->rect());
    w->show();
    w->raise();
}

void VideoPane::setEngineLabel(const QString& name)
{
    if (m_engineLabel != 0)
        m_engineLabel->setText(name);
}

void VideoPane::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_surface != 0)
        m_surface->setGeometry(0, m_surface->y(), width(), m_surface->height());
    // 渲染器子件跟随 surface(不跟则停在初建尺寸,画面缩在一角)
    if (m_renderer != 0 && m_surface != 0)
        m_renderer->setGeometry(0, 0, m_surface->width(), m_surface->height());
}
