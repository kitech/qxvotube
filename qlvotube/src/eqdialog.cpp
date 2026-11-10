#include "eqdialog.h"
#include "compat34.h"
#include <qslider.h>
#include <qlabel.h>
#include <qpushbutton.h>

static void addRow(QBoxLayout* lay, QWidget* parent, const char* title,
                   QSlider*& sl, QLabel*& val, int lo, int hi)
{
    QWidget* row = new QWidget(parent);
    QBoxLayout* rl = qNewBoxLayout(row, QBoxLayout::LeftToRight, 4, 4);
    QLabel* name = new QLabel(qFromUtf8(title), row);
    name->setFixedWidth(96);
    sl = new QSlider(Qt::Horizontal, row);
    sl->setRange(lo, hi);
    sl->setValue(0);
    val = new QLabel(qFromUtf8("0"), row);
    val->setFixedWidth(36);
    rl->addWidget(name, 0);
    rl->addWidget(sl, 1);
    rl->addWidget(val, 0);
    lay->addWidget(row, 0);
}

CXVideoEqDialog::CXVideoEqDialog(QWidget* parent)
    : QDialog(parent), m_br(0), m_ct(0), m_sat(0), m_hue(0),
      m_brVal(0), m_ctVal(0), m_satVal(0), m_hueVal(0)
{
#ifdef QT3_BUILD
    setCaption(qFromUtf8("音视频均衡器"));
#else
    setWindowTitle(qFromUtf8("音视频均衡器"));
#endif
    QBoxLayout* lay = qNewBoxLayout(this, QBoxLayout::TopToBottom, 10, 6);
    addRow(lay, this, "亮度", m_br, m_brVal, -100, 100);
    addRow(lay, this, "对比度", m_ct, m_ctVal, -100, 100);
    addRow(lay, this, "饱和度", m_sat, m_satVal, -100, 100);
    addRow(lay, this, "色相", m_hue, m_hueVal, -180, 180);

    QWidget* bottom = new QWidget(this);
    QBoxLayout* bl = qNewBoxLayout(bottom, QBoxLayout::LeftToRight, 4, 4);
    QPushButton* reset = new QPushButton(qFromUtf8("重置"), bottom);
    bl->addWidget(reset, 0);
    bl->addStretch(1);
    lay->addWidget(bottom, 0);
    QObject::connect(reset, SIGNAL(clicked()), this, SLOT(resetEq()));

    QSlider* all[4] = { m_br, m_ct, m_sat, m_hue };
    for (int i = 0; i < 4; i++)
        QObject::connect(all[i], SIGNAL(valueChanged(int)),
                         this, SLOT(onAnySlider(int)));
    resize(360, 220);
}

CXVideoEq CXVideoEqDialog::eq() const
{
    CXVideoEq e;
    e.brightness = m_br->value();
    e.contrast   = m_ct->value();
    e.saturation = m_sat->value();
    e.hue        = m_hue->value();
    return e;
}

void CXVideoEqDialog::resetEq()
{
    m_br->setValue(0);
    m_ct->setValue(0);
    m_sat->setValue(0);
    m_hue->setValue(0);
}

void CXVideoEqDialog::onAnySlider(int)
{
    m_brVal->setNum(m_br->value());
    m_ctVal->setNum(m_ct->value());
    m_satVal->setNum(m_sat->value());
    m_hueVal->setNum(m_hue->value());
    emit eqChanged();
}
