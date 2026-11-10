#ifndef RENDERER_GL_H
#define RENDERER_GL_H

#include "cxrenderer.h"
#include "cxvideo_global.h"

#ifdef QT3_BUILD
#include <qgl.h> // QGLWidget(Qt3/Qt4 固定管线)
#else
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#endif

#include <qimage.h>

// GL 渲染器(M4-A;detail.md §7.6)
// - 无 Q_OBJECT(无信号/槽;moc 不解析条件基类段)
// - Qt3/Qt4:QGLWidget 固定管线 + RGB32 纹理 quad;EQ 走 CPU LUT
//   (cxEqApplyRgb 预处理后上传;固定管线不赌 glColorMatrix 扩展)
// - Qt5.4+/Qt6:QOpenGLWidget core profile;fragment shader uniforms
//   (brightness/contrast/saturation/**hue mat3 旋转**)
class CXRendererGl
#ifdef QT3_BUILD
    : public QGLWidget, public CXVideoRenderer {
#else
    : public QOpenGLWidget, public CXVideoRenderer, protected QOpenGLFunctions {
#endif
public:
    explicit CXRendererGl(QWidget* parent = 0);

    void showFrame(const CXVideoFrame* f);
    void setViewport(const QRect& contentRect);

    void setVideoEq(const CXVideoEq& eq);
    bool saveScreenshot(const QString& path);

protected:
    // 两 Qt 同名;Qt3=QGLWidget 虚,Qt6=QOpenGLWidget 虚(moc 不吃类内条件段)
    void initializeGL();
    void resizeGL(int w, int h);
    void paintGL();

private:
    void paintFrame(); // 公共帧呈现路径(上传 + quad;两 Qt 各自 GL 通路)

    QImage m_pendingImg; // showFrame 侧(UI 线程入)
    QImage m_lastImg;    // 拉回/截图备件
    unsigned int m_tex;
    CXVideoEq m_eq;
    unsigned char m_lut[256];
    bool m_eqOn; // Qt3 侧 CPU LUT 生效位
#ifdef QT3_BUILD
    // QGLWidget 固定管线无需 shader
#else
    QOpenGLShaderProgram* m_prog;
    QOpenGLBuffer* m_vbuf;  // core profile 必须走 VAO/VBO
    QOpenGLVertexArrayObject* m_vao;
    int m_uTex, m_uBri, m_uCt, m_uSat, m_uHue, m_uScale;
#endif
};

#endif // RENDERER_GL_H
