#ifndef VIDEOPANE_H
#define VIDEOPANE_H

#include <qwidget.h>
#include <qstring.h>

class QBoxLayout;
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

signals:
    void backRequested();

protected:
    void resizeEvent(QResizeEvent* e);

private:
    QWidget* m_surface;
    QWidget* m_renderer; // setRendererWidget 注入(尺寸随 surface)
    QBoxLayout* m_lay;
    QPushButton* m_backBtn;
    QLabel* m_engineLabel;
};

#endif // VIDEOPANE_H
