#ifndef VIDEOPANE_H
#define VIDEOPANE_H

#include <qwidget.h>
#include <qstring.h>

class QBoxLayout;
class QEvent;
class QPushButton;
class QLabel;

// M5-A:播放页容器(宿主窗:ffmpeg 的 CXRendererGl 作子件;mpv 的
// CXRendererMpvGl 由引擎自建并挂到这里)
class VideoPane : public QWidget {
    Q_OBJECT
public:
    explicit VideoPane(QWidget* parent = 0);

    QWidget* surface() const { return m_surface; } // 交给 setHostWidget
    void setRendererWidget(QWidget* w);            // ffmpeg 路:GL 渲染器
    void setEngineLabel(const QString& name);
    void setSubtitleText(const QString& text); // 空串=隐藏(M5-A3)
    // M7:顶栏右侧实时分辨率/档位(如 "1920×1080 · 720P")
    void setVideoInfoText(const QString& text);

signals:
    void backRequested();
    void contextMenuRequested(const QPoint& globalPos);   /* M7:播放器右键 */

protected:
    void resizeEvent(QResizeEvent* e);
    bool eventFilter(QObject* o, QEvent* e);
    void contextMenuEvent(QContextMenuEvent* e);          /* M7 */

private:
    QWidget* m_surface;
    QWidget* m_renderer; // setRendererWidget 注入(尺寸随 surface)
    QBoxLayout* m_lay;
    QPushButton* m_backBtn;
    QLabel* m_engineLabel;
    QLabel* m_infoLabel;      /* M7:分辨率/档位 */
    QLabel* m_subLabel;
};

#endif // VIDEOPANE_H
