#include "renderer_gl.h"
#include "cxeq.h"
#include "cxframe.h"
#include <qimage.h>
#include <math.h>
#include <string.h>

static inline float cxMinF(float a, float b) { return a < b ? a : b; }

#ifndef QT3_BUILD
#include <QSurfaceFormat>
// Qt6 core profile:GLSL 150(in/out/texture;Qt 自动拼 #version)
static const char* kGlVertSrc =
    "#version 150\n"
    "uniform vec2 uScale;\n"
    "in vec2 aPos;\n"
    "in vec2 aUV;\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "  vUV = aUV;\n"
    "  gl_Position = vec4(aPos * uScale, 0.0, 1.0);\n"
    "}\n";

static const char* kGlFragSrc =
    "#version 150\n"
    "uniform sampler2D uTex;\n"
    "uniform float uBri;\n"  // -64..64(0.25 级)
    "uniform float uCt;\n"   // 0.7..2.3
    "uniform float uSat;\n"  // 0..2
    "uniform mat3 uHue;\n"
    "in vec2 vUV;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  vec4 c = texture(uTex, vUV);\n"
    "  vec3 rgb = (c.rgb - 0.5) * uCt + 0.5 + uBri / 255.0;\n"
    "  float luma = dot(rgb, vec3(0.299, 0.587, 0.114));\n"
    "  rgb = uHue * rgb;\n"
    "  rgb = vec3(luma) + (rgb - vec3(luma)) * uSat;\n"
    "  fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);\n"
    "}\n";
#endif

// hue → 3x3 旋转(标准 luma 保持色相旋转;行主序,上传时 transpose=GL_TRUE)
static void cxHueMatrix(int hue, float* m)
{
    double th = 3.14159265358979323846 / 180.0 * hue;
    double c = cos(th), s = sin(th);
    m[0] = (float)(0.299 + 0.701 * c + 0.168 * s);
    m[1] = (float)(0.587 - 0.587 * c + 0.330 * s);
    m[2] = (float)(0.114 - 0.114 * c - 0.497 * s);
    m[3] = (float)(0.299 - 0.299 * c - 0.328 * s);
    m[4] = (float)(0.587 + 0.413 * c + 0.035 * s);
    m[5] = (float)(0.114 - 0.114 * c + 0.292 * s);
    m[6] = (float)(0.299 - 0.300 * c + 1.250 * s);
    m[7] = (float)(0.587 - 0.588 * c - 1.050 * s);
    m[8] = (float)(0.114 + 0.886 * c - 0.203 * s);
}

CXRendererGl::CXRendererGl(QWidget* parent)
#ifdef QT3_BUILD
    : QGLWidget(parent, 0)
#else
    : QOpenGLWidget(parent)
#endif
      , m_tex(0), m_eqOn(false)
{
    setMinimumSize(320, 180);
#ifndef QT3_BUILD
    // Qt6:显式 core profile(#version 150 shader 的硬前提;默认兼容上下文
    // 会把 shader 当 GLSL 1.10 编译,in/out 直接报错)
    QSurfaceFormat fmt = format();
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(fmt);
    m_progReady = false;
#endif
    cxEqBuildLut(m_eq, m_lut);
#ifndef QT3_BUILD
    m_prog = 0;
    m_vbuf = 0;
    m_vao = 0;
    m_uTex = -1;
    m_uBri = -1;
    m_uCt = -1;
    m_uSat = -1;
    m_uHue = -1;
    m_uScale = -1;
#endif
}

void CXRendererGl::showFrame(const CXVideoFrame* f)
{
    if (f == 0)
        return;
    // Qt3:CPU LUT 在入侧应用(QGLWidget 无 shader);Qt6:原帧入,shader 时变
#ifdef QT3_BUILD
    m_pendingImg = (m_eqOn ? (cxEqApplyRgb(f->img, m_lut,
                                           cxEqSatPermil(m_eq))) : f->img);
#else
    m_pendingImg = f->img;
#endif
#ifdef QT3_BUILD
    updateGL();
#else
    update();
#endif
}

void CXRendererGl::setViewport(const QRect& contentRect)
{
    (void)contentRect; // letterbox 走 glViewport/quad 缩放,不依赖 contentRect
}

void CXRendererGl::setVideoEq(const CXVideoEq& eq)
{
    m_eq = eq;
    cxEqBuildLut(m_eq, m_lut);
    m_eqOn = !cxEqNeutral(m_eq);
#ifdef QT3_BUILD
    updateGL();
#else
    update();
#endif
}

bool CXRendererGl::saveScreenshot(const QString& path)
{
    if (m_pendingImg.isNull())
        return false;
#ifndef QT3_BUILD
    makeCurrent();
    QImage img = grabFramebuffer(); // QOpenGLWidget 自带回读
    doneCurrent();
#else
    QImage img = m_lastImg.isNull() ? m_pendingImg : m_lastImg;
#endif
    return img.save(path, "PNG");
}

// ── Qt3:固定管线 ──
#ifdef QT3_BUILD

void CXRendererGl::initializeGL()
{
    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, (GLuint*)&m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

void CXRendererGl::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void CXRendererGl::paintGL()
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    paintFrame();
}

void CXRendererGl::paintFrame()
{
    if (m_pendingImg.isNull())
        return;
    QImage img = m_pendingImg;
    if (img.depth() != 32)
        img = img.convertDepth(32);
    if (img.width() <= 0 || img.height() <= 0)
        return;
    // QImage RGB32 小端 BGRX → rgbSwapped 转 RGBA 上传(免 GL_BGRA 扩展赌注)
    img = img.swapRGB();
    m_lastImg = m_pendingImg;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    int iw = img.width(), ih = img.height();
    float sc = cxMinF((float)width() / iw, (float)height() / ih);
    float dw = iw * sc, dh = ih * sc;
    float dx = (width() - dw) / 2.0f, dy = (height() - dh) / 2.0f;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(dx, dy);
    glTexCoord2f(1, 0); glVertex2f(dx + dw, dy);
    glTexCoord2f(1, 1); glVertex2f(dx + dw, dy + dh);
    glTexCoord2f(0, 1); glVertex2f(dx, dy + dh);
    glEnd();
}

#else // ── Qt5.4+/Qt6:core profile + shader ──

static const float kGlQuad[8] = { // 全屏 quad 位置(TRIANGLE_FAN)
    -1.0f, -1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
    -1.0f,  1.0f
};
static const float kGlQuadUV[8] = {
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f
};

void CXRendererGl::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0, 0, 0, 1);
    m_prog = new QOpenGLShaderProgram(this);
    m_prog->addShaderFromSourceCode(QOpenGLShader::Vertex, kGlVertSrc);
    m_prog->addShaderFromSourceCode(QOpenGLShader::Fragment, kGlFragSrc);
    m_prog->bindAttributeLocation("aPos", 0);
    m_prog->bindAttributeLocation("aUV", 1);
    m_progReady = m_prog->link(); // 失败 → paintGL 早退(不再黑人眼)
    m_uTex = m_prog->uniformLocation("uTex");
    m_uBri = m_prog->uniformLocation("uBri");
    m_uCt = m_prog->uniformLocation("uCt");
    m_uSat = m_prog->uniformLocation("uSat");
    m_uHue = m_prog->uniformLocation("uHue");
    m_uScale = m_prog->uniformLocation("uScale");
    glGenTextures(1, &m_tex);

    // VAO + 交错 VBO(aPos|aUV 各 2 float);core profile 上传/绘制均经此
    m_vao = new QOpenGLVertexArrayObject(this);
    m_vao->create();
    m_vbuf = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    m_vbuf->create();
    m_vao->bind();
    m_vbuf->bind();
    float verts[16]; // 4 顶点 × (x,y,u,v)
    for (int i = 0; i < 4; i++) {
        verts[i * 4] = kGlQuad[i * 2];
        verts[i * 4 + 1] = kGlQuad[i * 2 + 1];
        verts[i * 4 + 2] = kGlQuadUV[i * 2];
        verts[i * 4 + 3] = kGlQuadUV[i * 2 + 1];
    }
    m_vbuf->allocate(verts, 16 * sizeof(float));
    m_prog->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_prog->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2,
                               4 * sizeof(float));
    m_prog->enableAttributeArray(0);
    m_prog->enableAttributeArray(1);
    m_vbuf->release();
    m_vao->release();
}

void CXRendererGl::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void CXRendererGl::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (m_pendingImg.isNull())
        return;
    QImage img = m_pendingImg.rgbSwapped(); // BGRX → RGBA
    if (m_prog == 0 || !m_prog->bind())
        return;
    m_lastImg = m_pendingImg;
    m_prog->setUniformValue(m_uTex, 0);
    m_prog->setUniformValue(m_uBri, (float)(m_eq.brightness * 64 / 100));
    m_prog->setUniformValue(m_uCt, (float)(1.0 + m_eq.contrast * 0.03));
    m_prog->setUniformValue(m_uSat, (float)cxEqSatPermil(m_eq) / 1000.0f);
    float hfm[9];
    cxHueMatrix(m_eq.hue, hfm);
    // QMatrix3x3 是行主序,Qt 上传按列主序读 → transpose=GL_TRUE 保持一致
    glUniformMatrix3fv(m_uHue, 1, GL_TRUE, hfm);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());

    // letterbox:VBO 固定全屏 quad,经 uScale(NDC 内比例)留黑边
    int iw = img.width(), ih = img.height();
    int w = width(), h = height();
    float sc = cxMinF((float)w / iw, (float)h / ih);
    float ndw = iw * sc / w, ndh = ih * sc / h;
    m_prog->setUniformValue(m_uScale, (float)ndw, (float)ndh);
    m_vao->bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    m_vao->release();
    m_prog->release();
}
#endif
