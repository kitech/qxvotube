#ifndef EQDIALOG_H
#define EQDIALOG_H

#include <qdialog.h>
#include "cxvideo_global.h"   /* CXVideoEq */

class QSlider;
class QLabel;

// M5-A2:音视频均衡器(4 滑杆;变更即发 eqChanged,主窗读 eq() 应用)
class CXVideoEqDialog : public QDialog {
    Q_OBJECT
public:
    explicit CXVideoEqDialog(QWidget* parent = 0);

    CXVideoEq eq() const;          // 当前四值

signals:
    void eqChanged();

public slots:
    void resetEq();                // 归零

private slots:
    void onAnySlider(int v);

private:
    QSlider* m_br;
    QSlider* m_ct;
    QSlider* m_sat;
    QSlider* m_hue;
    QLabel* m_brVal;
    QLabel* m_ctVal;
    QLabel* m_satVal;
    QLabel* m_hueVal;
};

#endif // EQDIALOG_H
