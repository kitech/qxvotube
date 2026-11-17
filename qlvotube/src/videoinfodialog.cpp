#include "videoinfodialog.h"
#include "cxengine.h"          // CxVideoInfo
#include <qapplication.h>
#include <qclipboard.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qpushbutton.h>

// 地址展示脱敏:签名/参数过长时截断(完整地址走"复制完整地址")
static QString cxMaskUrl(const QString& url)
{
    if (url.length() <= 96)
        return url;
    return url.left(96) + qFromUtf8("…");
}

VideoInfoDialog::VideoInfoDialog(const CxVideoInfo& info, QWidget* parent)
    : QDialog(parent), m_url(info.url)
{
#ifdef QT3_BUILD
    setCaption(qFromUtf8("视频详情"));
#else
    setWindowTitle(qFromUtf8("视频详情"));
#endif
    QBoxLayout* lay = qNewBoxLayout(this, QBoxLayout::TopToBottom, 10, 10);
    const QString na = qFromUtf8("N/A");

    QString res = na;
    if (info.width > 0 && info.height > 0)
        res = qFromUtf8("%1×%2").arg(info.width).arg(info.height);
    QString br = na;
    if (info.bitrateKbps > 0)
        br = QString::fromLatin1("%1 kbps").arg(info.bitrateKbps);
    QString fps = na;
    if (info.fps > 0.01)
        fps = QString::fromLatin1("%1 fps").arg(info.fps, 0, 'f', 2);
    QString q = info.quality.isEmpty() ? na : info.quality;
    if (info.degraded && !info.requestedQuality.isEmpty())
        q += qFromUtf8("(请求 ") + info.requestedQuality + qFromUtf8(",已降级)");

    const int kN = 7;
    QString keys[kN];
    QString vals[kN];
    keys[0] = qFromUtf8("分辨率");   vals[0] = res;
    keys[1] = qFromUtf8("码流");     vals[1] = br;
    keys[2] = qFromUtf8("解码器");   vals[2] = info.codec.isEmpty() ? na : info.codec;
    keys[3] = qFromUtf8("硬解");     vals[3] = info.hw.isEmpty() ? qFromUtf8("软解") : info.hw;
    keys[4] = qFromUtf8("帧率");     vals[4] = fps;
    keys[5] = qFromUtf8("档位");     vals[5] = q;
    keys[6] = qFromUtf8("地址");     vals[6] = cxMaskUrl(info.url.isEmpty() ? na : info.url);

    for (int i = 0; i < kN; ++i) {
        QBoxLayout* h = qNewBoxLayout(0, QBoxLayout::LeftToRight, 6, 0);
        QLabel* k = new QLabel(keys[i] + qFromUtf8("："), this);
        QLabel* v = new QLabel(vals[i], this);
        k->setFixedWidth(64);
        v->setMinimumWidth(360);
        h->addWidget(k, 0);
        h->addWidget(v, 1);
        lay->addLayout(h);
    }

    QBoxLayout* btns = qNewBoxLayout(0, QBoxLayout::LeftToRight, 6, 0);
    QPushButton* copy = new QPushButton(qFromUtf8("复制完整地址"), this);
    QPushButton* close = new QPushButton(qFromUtf8("关闭"), this);
    QObject::connect(copy, SIGNAL(clicked()), this, SLOT(copyUrl()));
    QObject::connect(close, SIGNAL(clicked()), this, SLOT(accept()));
    btns->addStretch(1);
    btns->addWidget(copy, 0);
    btns->addWidget(close, 0);
    lay->addLayout(btns);

    resize(560, 300);
}

void VideoInfoDialog::copyUrl()
{
    if (m_url.isEmpty())
        return;
    QApplication::clipboard()->setText(m_url);
}
