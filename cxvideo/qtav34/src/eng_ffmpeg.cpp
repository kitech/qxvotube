#include "compat34.h"
#include "eng_ffmpeg.h"
#include "cxengine.h"
#include "cxrenderer.h"
#include "cxff_wrap.h"
#include "cxpqueue.h"
#include "cxffdemux.h"
#include "cxffdec.h"
#include "cxffdeca.h"
#include "cffao.h"
#include "cxffclock.h"
#include "cxffvthread.h"
#include "cxffathread.h"
#include "cxframe.h"
#include <stdlib.h>
#include <string>
#include <qapplication.h>
#include <qstring.h>
#include <stdio.h>

// ── 引擎 ──

CXEngineFfmpeg::CXEngineFfmpeg()
    : CXEngine(), m_fmtCtx(0), m_vq(0), m_aq(0), m_dec(0), m_adec(0),
      m_demux(0), m_vthread(0), m_athread(0), m_ao(0), m_clock(0),
      m_paused(false), m_running(false),
      m_durMs(0), m_curMs(0), m_vIdx(-1), m_aIdx(-1),
      m_seekPend(0), m_eosPend(0)
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

    // M2.1 音频链:CxClock + AO + adec + athread(失败逐级降级不拖垮视频)
    m_clock = new CxClock();
    m_ao = 0;
    m_adec = 0;
    m_athread = 0;
    bool aoFallback = false;
    if (m_aIdx >= 0) {
        QString aerr;
        m_adec = new CxDecoderAudio();
        if (!m_adec->init(fmt->streams[m_aIdx], &aerr)) {
            delete m_adec; // 降级:M2.0 盲等路径
            m_adec = 0;
            m_aIdx = -1;
            delete m_clock;
            m_clock = 0;
        } else {
            const char* env = getenv("CX_FF_AO");
            std::string want(env != 0 && env[0] != '\0' ? env : "auto");
            m_ao = cffaoCreate(want.c_str());
            if (m_ao == 0 || !m_ao->open(m_adec->inCh(), m_adec->inRate())) {
                // 指名后端被编出/open 失败:silent 兜底,继续播(真实后端再诊断)
                if (m_ao != 0) {
                    m_ao->close();
                    delete m_ao;
                }
                m_ao = cffaoNewSilent();
                if (m_ao != 0 && !m_ao->open(m_adec->inCh(), m_adec->inRate())) {
                    delete m_ao;
                    m_ao = 0;
                }
                aoFallback = true;
            }
            if (m_ao == 0) {
                delete m_adec;
                m_adec = 0;
                m_aIdx = -1;
                delete m_clock;
                m_clock = 0;
            }
        }
    } else {
        delete m_clock;
        m_clock = 0;
    }

    m_demux = new CxDemuxThread(fmt, m_vq, m_vIdx, m_aq, m_aIdx);
    m_vthread = new CXFFmpegVideoThread(m_vq, m_dec, this);
    m_vthread->setRate(1.0);
    if (m_clock != 0)
        m_vthread->setClock(m_clock);
    if (m_ao != 0)
        m_vthread->setAo(m_ao);
    if (m_adec != 0 && m_ao != 0)
        m_athread = new CXFFmpegAudioThread(m_aq, m_adec, m_ao, m_clock, this);
    m_demux->start();
    m_vthread->start();
    if (m_athread != 0)
        m_athread->start();
    m_running = true;
    m_eosPend = 1 + (m_athread != 0 ? 1 : 0); // 双线程都结束才 finished

    if (aoFallback)
        emit noticedError((int)CxErrAudio, m_url); // 非致命:silent 续播
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
    // 音频后端阻塞写可能不可中断(pulse-simple):超时则放弃回收
    // (线程/解码器/AO/时钟/队列一并泄漏),避免 delete 运行中线程崩溃
    bool audioLeak = false;
    if (m_athread != 0) {
        m_athread->requestStop();
        if (!m_athread->wait(3000)) {
            audioLeak = true;
            m_athread = 0; // 泄漏,不 delete
        } else {
            delete m_athread;
            m_athread = 0;
        }
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
    if (!audioLeak && m_aq != 0) {
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
    if (!audioLeak) {
        if (m_adec != 0) {
            m_adec->close();
            delete m_adec;
            m_adec = 0;
        }
        if (m_ao != 0) {
            m_ao->close();
            delete m_ao;
            m_ao = 0;
        }
        if (m_clock != 0) {
            delete m_clock;
            m_clock = 0;
        }
    } else {
        m_adec = 0;
        m_ao = 0;
        m_clock = 0;
        m_aq = 0;
    }
    if (m_fmtCtx != 0) {
        avformat_close_input((AVFormatContext**)&m_fmtCtx);
        m_fmtCtx = 0;
    }
    m_running = false;
    m_paused = false;
    m_curMs = 0;
    m_seekPend = 0;
    m_eosPend = 0;
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
        if (m_athread != 0)
            m_athread->setPaused(false);
        if (m_ao != 0)
            m_ao->resume();
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
        if (m_athread != 0)
            m_athread->setPaused(true);
        if (m_ao != 0)
            m_ao->pause();
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
    if (m_fmtCtx == 0)
        return false;
    int sIdx = (m_vIdx >= 0) ? m_vIdx : m_aIdx; // 纯音频亦能 seek
    if (sIdx < 0)
        return false;
    AVRational tb = FMT_CTX()->streams[sIdx]->time_base;
    long long target = av_rescale_q(ms, AV_TIME_BASE_Q, tb);
    if (av_seek_frame(FMT_CTX(), sIdx, target, AVSEEK_FLAG_BACKWARD) < 0) {
        emit noticedError((int)CxErrSeek, m_url);
        return false;
    }
    if (m_dec != 0)
        m_dec->flush();
    if (m_adec != 0)
        m_adec->flush();
    if (m_vq != 0)
        m_vq->reset();
    if (m_aq != 0)
        m_aq->reset();
    if (m_ao != 0)
        m_ao->clear();
    if (m_clock != 0)
        m_clock->reset();
    if (m_vthread != 0)
        m_vthread->setRenderPts0(ms);
    if (m_athread != 0)
        m_athread->setRenderPts0(ms);
    m_seekPend = 1;
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
    if (m_athread != 0)
        m_athread->setRate(r);
    if (m_clock != 0)
        m_clock->setSpeed(r);
    return true;
}

bool CXEngineFfmpeg::setVolume(int pct)
{
    if (m_athread == 0)
        return false;
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    m_athread->setVolume(pct);
    return true;
}

bool CXEngineFfmpeg::setMute(bool on)
{
    if (m_athread == 0)
        return false;
    m_athread->setMute(on);
    return true;
}

const char* CXEngineFfmpeg::aoBackend() const
{
    return m_ao != 0 ? m_ao->backend() : 0;
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
        if (m_running) {
            if (m_eosPend > 1) {
                m_eosPend--; // 另一线程未完(纯音频/拖尾)
            } else {
                m_eosPend = 0;
                m_running = false;
                emit finished();
            }
        }
        break;
    case CxFFmpegEvSeekDone:
        if (m_seekPend) {
            m_seekPend = 0;
            emit seekFinished(ev->ms);
        }
        break;
    case CxFFmpegEvAudioPos:
        // 视频缺席(纯音频)时 position 由音频时钟驱动;有视频时以帧为准
        if (m_vIdx < 0) {
            m_curMs = ev->ms;
            emit positionChanged(ev->ms);
        }
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
