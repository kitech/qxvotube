#ifndef VIDEOTHUMBCACHE_H
#define VIDEOTHUMBCACHE_H

#include "qlcompat34.h"
#include "eventpoller.h"

#include <qwidget.h>
#include <qstring.h>
#include <qpixmap.h>
#include <qimage.h>

/* 视频封面异步缓存(EventPoller/curl 取字节 → 泵线程 QImage 解码 →
 * postEvent 回 GUI 线程转 QPixmap)。
 *
 * 口径:
 *   - 同一 URL 只请求一次;失败也记账,不重试(避免滚动时反复打站点)
 *   - Referer 按封面域名推导(站点图床有防盗链)
 *   - 代理沿用 QXVOTUBE_PROXY(与 providers 同口径)
 *   - 全部状态只在 GUI 线程读写;泵线程只做解码 + postEvent
 */
const EventType34 ThumbReadyEventType = toEventType34(QEvent::User + 121);

class ThumbReadyEvent : public CustomEventBase {
public:
    ThumbReadyEvent(const QString& u, const QImage& i)
        : CustomEventBase(ThumbReadyEventType), url(u), image(i) {}
    QString url;
    QImage image;
};

namespace thumbcache {

// 请求封面(未命中且未请求过才发)。target 接收 ThumbReadyEvent。
void request(const QString& url, QWidget* target);

// GUI 线程取已解码封面(未就绪 → 空 QPixmap)
QPixmap pixmap(const QString& url);

// 诊断计数:请求数 / 失败数(用于 QLVOTUBE_DEBUG 日志)
int requested();
int failed();

// 事件落地(ContentView::customEvent 调用;GUI 线程)
void accept(const QString& url, const QImage& image);

} // namespace thumbcache

#endif // VIDEOTHUMBCACHE_H
