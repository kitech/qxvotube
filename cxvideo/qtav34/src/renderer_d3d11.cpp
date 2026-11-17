#include "renderer_d3d11.h"

#if defined(CX_OS_WIN) && defined(WITH_CX_RENDERER_D3D11)

#include <qimage.h>
#include <qevent.h>
#include <string.h>
#include <d3d11.h>
#include <dxgi1_2.h>

CXRendererD3D11::CXRendererD3D11(QWidget* parent)
    : QWidget(parent), m_dev(0), m_ctx(0), m_sc(0), m_stage(0),
      m_sw(0), m_sh(0), m_bw(0), m_bh(0)
{
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setTargetSize(size());
}

CXRendererD3D11::~CXRendererD3D11()
{
    freeD3d();
}

// Qt4/5/6 在 Windows 上默认会自己绘制 widget 背景;
// 返回 0 的 paintEngine 只在 Qt4 有效,Qt5+ 用 WA_PaintOnScreen 即可。
void CXRendererD3D11_paintEngineGuard() { }

bool CXRendererD3D11::isAvailable()
{
    D3D_FEATURE_LEVEL lv;
    ID3D11Device* d = 0;
    HRESULT hr = D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, 0, 0,
                                   D3D11_SDK_VERSION, &d, &lv, 0);
    if (hr == S_OK && d != 0) {
        d->Release();
        return true;
    }
    hr = D3D11CreateDevice(0, D3D_DRIVER_TYPE_WARP, 0,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, 0, 0,
                           D3D11_SDK_VERSION, &d, &lv, 0);
    if (hr == S_OK && d != 0) {
        d->Release();
        return true;
    }
    return false;
}

void CXRendererD3D11::freeD3d()
{
    if (m_stage != 0) { m_stage->Release(); m_stage = 0; }
    if (m_sc != 0)    { m_sc->Release();    m_sc = 0; }
    if (m_ctx != 0)   { m_ctx->Release();   m_ctx = 0; }
    if (m_dev != 0)   { m_dev->Release();   m_dev = 0; }
    m_sw = m_sh = m_bw = m_bh = 0;
}

bool CXRendererD3D11::initD3d()
{
    if (m_sc != 0)
        return true;
    D3D_FEATURE_LEVEL lv;
    HRESULT hr = D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, 0, 0,
                                   D3D11_SDK_VERSION, &m_dev, &lv, &m_ctx);
    if (hr != S_OK)
        hr = D3D11CreateDevice(0, D3D_DRIVER_TYPE_WARP, 0,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, 0, 0,
                               D3D11_SDK_VERSION, &m_dev, &lv, &m_ctx);
    if (hr != S_OK || m_dev == 0 || m_ctx == 0)
        return false;

    IDXGIDevice* dxdev = 0;
    IDXGIAdapter* ad = 0;
    IDXGIFactory2* fac = 0;
    if (m_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev) != S_OK)
        return false;
    if (dxdev->GetAdapter(&ad) == S_OK && ad != 0)
        ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);
    if (ad != 0)
        ad->Release();
    dxdev->Release();
    if (fac == 0)
        return false;

    DXGI_SWAP_CHAIN_DESC1 d;
    ZeroMemory(&d, sizeof(d));
    d.Width = (UINT)width();
    d.Height = (UINT)height();
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // DISCARD 模型 + 单缓冲:允许 CopyResource 直接写后台缓冲
    d.BufferCount = 1;
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    d.Scaling = DXGI_SCALING_STRETCH;
    hr = fac->CreateSwapChainForHwnd(m_dev, (HWND)winId(), &d, 0, 0, &m_sc);
    fac->Release();
    if (hr != S_OK || m_sc == 0) {
        m_sc = 0;
        return false;
    }
    m_bw = width();
    m_bh = height();
    return true;
}

bool CXRendererD3D11::ensureStaging(int w, int h)
{
    if (m_stage != 0 && m_sw == w && m_sh == h)
        return true;
    if (m_stage != 0) {
        m_stage->Release();
        m_stage = 0;
    }
    D3D11_TEXTURE2D_DESC d;
    ZeroMemory(&d, sizeof(d));
    d.Width = (UINT)w;
    d.Height = (UINT)h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (m_dev->CreateTexture2D(&d, 0, &m_stage) != S_OK) {
        m_stage = 0;
        return false;
    }
    m_sw = w;
    m_sh = h;
    return true;
}

bool CXRendererD3D11::present(const QImage& img, const QRect& content)
{
    if (m_sc == 0 && !initD3d())
        return false;
    QSize ws = size();
    if (ws.width() <= 0 || ws.height() <= 0)
        return true;
    if (ws.width() != m_bw || ws.height() != m_bh) {
        if (m_stage != 0) {
            m_stage->Release();
            m_stage = 0;
        }
        if (m_sc->ResizeBuffers(0, (UINT)ws.width(), (UINT)ws.height(),
                                DXGI_FORMAT_UNKNOWN, 0) != S_OK)
            return false;
        m_bw = ws.width();
        m_bh = ws.height();
    }
    QImage src = img;
    src = cxToRgb32(src);
    if (src.width() != content.width() || src.height() != content.height())
        src = cxScaleRgb(src, content.width(), content.height());
    if (!ensureStaging(ws.width(), ws.height()))
        return false;

    D3D11_MAPPED_SUBRESOURCE m;
    if (m_ctx->Map(m_stage, 0, D3D11_MAP_WRITE_DISCARD, 0, &m) != S_OK)
        return false;
    ::memset(m.pData, 0, (size_t)m.RowPitch * ws.height());
    for (int y = 0; y < src.height(); ++y) {
        char* dst = (char*)m.pData + (y + content.y()) * m.RowPitch
                    + content.x() * 4;
        ::memcpy(dst, src.scanLine(y), (size_t)src.width() * 4);
    }
    m_ctx->Unmap(m_stage, 0);

    ID3D11Texture2D* back = 0;
    if (m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back) != S_OK)
        return false;
    m_ctx->CopyResource(back, m_stage);
    back->Release();
    m_sc->Present(1, 0);
    return true;
}

void CXRendererD3D11::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    setTargetSize(e->size());
    if (!m_img.isNull())
        present(m_img, letterbox(e->size(), m_img.size()));
}

#endif
