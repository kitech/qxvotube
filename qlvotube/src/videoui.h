#ifndef VIDEOUI_H
#define VIDEOUI_H

#include "compat34.h"
#include "eventpoller.h"

#include <qstring.h>
#include <qstringlist.h>
#include <qlist.h>
#include <vector>

/* providers 回调运行在 httplib 工作线程 → 统一 postEvent 回 GUI 线程 */

struct VideoCardData {
    QString id, title, author, cover;
    qint64 durationMs;
    qint64 playCount;
    qint64 publishMs;      /* M7:发布时间(0=未知) */
    QString webUrl;        /* M7:网页地址(空=该源未提供) */
    VideoCardData() : durationMs(0), playCount(0), publishMs(0) {}
};

const EventType34 VideoUiEventType = toEventType34(QEvent::User + 120);

class VideoUiEvent : public CustomEventBase {
public:
    enum Kind { Categories, Page, Error, Status };
    VideoUiEvent(Kind k)
        : CustomEventBase(VideoUiEventType), kind(k), hasMore(false),
          loading(false), degraded(false), page(0), gen(0) {}
    Kind kind;
    QStringList catKeys, catNames;   /* Categories */
    std::vector<VideoCardData> cards;      /* Page */
    QString text;                    /* Error/Status 文案;Page/Categories 带站点前缀 */
    QString chipKey;                 /* Page: 本次分类;Status: 视频 id */
    QString referer, userAgent, proxy;  /* Status: 直链所需网络选项 */
    /* M7:档位信息(实际/请求/是否降级) */
    QString quality, requestedQuality;
    bool degraded;
    bool hasMore;
    bool loading;
    int page;
    int gen;            /* 请求代际:与 MainWindow::m_gridGen 不符即丢弃 */
};

#endif // VIDEOUI_H
