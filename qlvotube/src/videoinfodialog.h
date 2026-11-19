#ifndef VIDEOINFODIALOG_H
#define VIDEOINFODIALOG_H

#include "qlcompat34.h"
#include <qdialog.h>
#include <qstring.h>

struct CxVideoInfo;

// M7:播放统计(B 站"视频统计信息"风格):地址/分辨率/码率/解码器/帧率/档位
class VideoInfoDialog : public QDialog {
    Q_OBJECT
public:
    VideoInfoDialog(const CxVideoInfo& info, QWidget* parent = 0);

private slots:
    void copyUrl();

private:
    QString m_url;
};

#endif // VIDEOINFODIALOG_H
