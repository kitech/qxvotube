#include "compat34.h"
#include "eng_ffmpeg.h"
#include "cxengine.h"
#include "cxrenderer.h"
#include "cxff_wrap.h"
#include "cxpqueue.h"
#include "cxffdemux.h"
#include "cxffdec.h"
#include "cxffvthread.h"
#include "cxframe.h"
#include <qapplication.h>
#include <qstring.h>
#include <stdio.h>

// ── 引擎 ──

CXEngineFfmpeg::CXEngineFfmpeg()
    : CXEngine(), m_fmtCtx(0), m_vq(0), m_aq(0), m_dec(0),
      m_demux(0), m_vthread(0), m_paused(false), m_running(false),
      m_durMs(0), m_curMs(0), m_vIdx(-1), m_aIdx(-1)
{
}

CXEngineFfmpeg::~CXEngineFfmpeg()
{
    close();
}

#define FMT_CTX() ((AVFormatContext*)m_fmtCtx)

bool CXEngineFfmpeg::open(const QString& url)
{
    if (m_fmtCtx != 0)
        close(); // 换源即重建

    m_url = url;
    m_paused = false;
    m_running = false;
    m_curMs = 0;
    m_vIdx = -1;
    m_aIdx = -1;

    QByteArray ba = qToUtf8(url);
    AVFormatContext* fmt = 0;
    if (avformat_open_input(&fmt, ba.data(), 0, 0) < 0) {
        emit noticedError((int)CxErrOpenFail, m_url);
        return false;
    }
    m_fmtCtx = fmt;
    if (avformat_find_stream_info(fmt, 0) < 0) {
        emit noticedError((int)CxErrOpenFail, m_url);
        close();
        return false;
    }
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        AVMediaType t = fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && m_vIdx < 0)
            m_vIdx = (int)i;
        else if (t == AVMEDIA_TYPE_AUDIO && m_aIdx < 0)
            m_aIdx = (int)i;
    }
    if (m_vIdx < 0 && m_aIdx < 0) {
        emit noticedError((int)CxErrNoStream, m_url);
        close();
        return false;
    }

    if (fmt->duration != AV_NOPTS_VALUE)
        m_durMs = (long)(fmt->duration / (AV_TIME_BASE / 1000));
    else
        m_durMs = 0;

    m_vq = new CxPacketQueue(256);
    m_aq = new CxPacketQueue(512);
    m_dec = new CxDecoder();
    if (m_vIdx >= 0) {
        QString err;
        if (!m_dec->init(fmt->streams[m_vIdx], &err)) {
            emit noticedError((int)CxErrDecode, err);
            close();
            return false;
        }
    }
    // M2.0 仅视频管线;音频流暂存到队列不消费(aIdx>=0 时 demux 仍推,浪费可忽略)
    m_demux = new CxDemuxThread(fmt, m_vq, m_vIdx, m_aq, m_aIdx);
    m_vthread = new CXFFmpegVideoThread(m_vq, m_dec, this);
    m_vthread->setRate(1.0);
    m_demux->start();
    m_vthread->start();
    m_running = true;

    emit started();
    if (m_durMs > 0)
        emit durationChanged(m_durMs);
    return true;
}

void CXEngineFfmpeg::stopThreads()
{
    if (m_vthread != 0) {
        m_vthread->requestStop();
        m_vthread->wait(2000);
        delete m_vthread;
        m_vthread = 0;
    }
    if (m_demux != 0) {
        m_demux->requestStop();
        m_demux->wait(2000);
        delete m_demux;
        m_demux = 0;
    }
    if (m_vq != 0) {
        m_vq->wake();
        m_vq->clear();
        delete m_vq;
        m_vq = 0;
    }
    if (m_aq != 0) {
        m_aq->wake();
        m_aq->clear();
        delete m_aq;
        m_aq = 0;
    }
    if (m_dec != 0) {
        m_dec->close();
        delete m_dec;
        m_dec = 0;
    }
    if (m_fmtCtx != 0) {
        avformat_close_input((AVFormatContext**)&m_fmtCtx);
        m_fmtCtx = 0;
    }
    m_running = false;
    m_paused = false;
    m_curMs = 0;
}

void CXEngineFfmpeg::close()
{
    stopThreads();
    m_url = QString();
    m_durMs = 0;
}

bool CXEngineFfmpeg::play()
{
    if (m_fmtCtx == 0)
        return false;
    if (m_paused) {
        m_paused = false;
        if (m_vthread != 0)
            m_vthread->setPaused(false);
    }
    return true;
}

bool CXEngineFfmpeg::pause()
{
    if (m_fmtCtx == 0)
        return false;
    if (!m_paused) {
        m_paused = true;
        if (m_vthread != 0)
            m_vthread->setPaused(true);
    }
    return true;
}

bool CXEngineFfmpeg::stop()
{
    if (m_fmtCtx == 0)
        return false;
    stopThreads();
    emit stopped();
    return true;
}

bool CXEngineFfmpeg::seek(long ms)
{
    if (m_fmtCtx == 0 || m_vIdx < 0)
        return false;
    AVStream* vs = FMT_CTX()->streams[m_vIdx];
    AVRational tb = vs->time_base;
    long long target = av_rescale_q(ms, AV_TIME_BASE_Q, tb);
    if (av_seek_frame(FMT_CTX(), m_vIdx, target, AVSEEK_FLAG_BACKWARD) < 0) {
        emit noticedError((int)CxErrSeek, m_url);
        return false;
    }
    m_dec->flush();
    m_vq->reset();
    if (m_vthread != 0)
        m_vthread->setRenderPts0(ms);
    return true;
}

bool CXEngineFfmpeg::setRate(double r)
{
    if (m_fmtCtx == 0)
        return false;
    if (r <= 0.0)
        r = 1.0;
    if (m_vthread != 0)
        m_vthread->setRate(r);
    return true;
}

bool CXEngineFfmpeg::setVolume(int)
{
    return false; // M2.1(音频链)实装
}

bool CXEngineFfmpeg::setMute(bool)
{
    return false;
}

long CXEngineFfmpeg::position() const
{
    return m_curMs;
}

long CXEngineFfmpeg::duration() const
{
    return m_durMs;
}

void CXEngineFfmpeg::customEvent(CustomEventBase* e)
{
    if (e->type() != toEventType34(kcxFFmpegEventRaw))
        return;
    dispatchEvent((CxFFmpegUiEvent*)e);
}

void CXEngineFfmpeg::dispatchEvent(const CxFFmpegUiEvent* ev)
{
    switch (ev->ev) {
    case CxFFmpegEvFrame: {
        if (ev->frame != 0) {
            m_curMs = ev->ms;
            if (p_renderer != 0)
                p_renderer->showFrame(ev->frame);
            emit positionChanged(ev->ms);
        }
        break;
    }
    case CxFFmpegEvFinished:
        m_running = false;
        emit finished();
        break;
    case CxFFmpegEvSeekDone:
        emit seekFinished(ev->ms);
        break;
    case CxFFmpegEvError:
        emit noticedError(ev->code, QString("ffmpeg"));
        break;
    case CxFFmpegEvDuration:
        m_durMs = ev->ms;
        emit durationChanged(ev->ms);
        break;
    default:
        break;
    }
}
