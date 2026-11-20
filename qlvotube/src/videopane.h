#ifndef VIDEOPANE_H
#define VIDEOPANE_H

#include <qwidget.h>
#include <qstring.h>

class QBoxLayout;
class QEvent;
class QPushButton;
class QLabel;
class QTimer;

// M5-A:播放页容器(宿主窗:ffmpeg 的 CXRendererGl 作子件;mpv 的
// CXRendererMpvGl 由引擎自建并挂到这里)
class VideoPane : public QWidget {
    Q_OBJECT
public:
    enum SurfaceHint { HintMidClick = 0, HintWheelUp = 1, HintWheelDown = 2 };

    explicit VideoPane(QWidget* parent = 0);

    QWidget* surface() const { return m_surface; } // 交给 setHostWidget
    void setRendererWidget(QWidget* w);            // ffmpeg 路:GL 渲染器
    void setEngineLabel(const QString& name);
    void setVideoTitle(const QString& title);       // 顶栏显示当前播放视频标题
    void setSubtitleText(const QString& text); // 空串=隐藏(M5-A3)
    // M7:顶栏右侧实时分辨率/档位(如 "1920×1080 · 720P")
    void setVideoInfoText(const QString& text);

signals:
    void backRequested();
    void contextMenuRequested(const QPoint& globalPos);   /* M7:播放器右键 */
    void togglePlayPauseRequested();   /* 画面单击 = 暂停/恢复 */
    void toggleFullscreenRequested();  /* 画面双击 = F */
    void surfaceHint(int hint);        /* 中键/滚轮:未实现提示(文案由 MainWindow i18n) */

private slots:
    void onSurfaceClickTimeout();

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
    QLabel* m_titleLabel;     /* 顶栏视频标题 */
    QLabel* m_infoLabel;      /* M7:分辨率/档位 */
    QLabel* m_subLabel;
    QTimer* m_clickTimer;     /* 单击/双击判定 */
};

#endif // VIDEOPANE_H
