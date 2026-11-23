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
#include "cffsthread.h"
#include "cxframe.h"
#include "cxrotate.h"
#include "limelog.h"
#include <stdlib.h>
#include <string>
#include <atomic>
#include <qapplication.h>
#include <qstring.h>
#include <qthread.h>
#include <qtimer.h>
#include <qmutex.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────
// 异步 open:把阻塞的 avformat_open_input / avformat_find_stream_info
// 移出 GUI 线程。interrupt_callback.opaque 恒指向引擎 m_openState.ioAbort
// (生命周期=引擎),绝不用 per-open ctx,避免 av_read_frame 期 UAF。
// ─────────────────────────────────────────────────────────────
const int kcxFFmpegOpenEventRaw = 4005;

struct CxFFmpegOpenCtx {
    CxFFmpegOpenCtx() : gen(0), cancel(false), pAbort(0), sinkAlive(true), sink(0) {}
    QString url;
    CxNetOptions net;
    int gen;
    std::atomic<bool> cancel;   // worker 自身取消判断
    volatile bool* pAbort;      // 指向引擎 m_openState.ioAbort(回调 opaque)
    QMutex mu;                  // 保护 sinkAlive + postEvent
    bool sinkAlive;
    QObject* sink;
};

class CxFFmpegOpenEvent : public CustomEventBase {
public:
    CxFFmpegOpenEvent(void* fmt, int rc, int gen)
        : CustomEventBase(toEventType34(kcxFFmpegOpenEventRaw)),
          fmt(fmt), rc(rc), gen(gen) {}
    void* fmt;                  // AVFormatContext*;customEvent 接手所有权
    int rc;
    int gen;
};

class CxFFmpegOpenThread : public QThread {
public:
    explicit CxFFmpegOpenThread(CxFFmpegOpenCtx* ctx) : m_ctx(ctx) {}
    static int interruptCb(void* opaque) {
        volatile bool* p = static_cast<volatile bool*>(opaque);
        return (p != 0 && *p) ? 1 : 0;
    }
    void run() {
        CxFFmpegOpenCtx* ctx = m_ctx;
        if (ctx == 0 || ctx->cancel.load()) return;
        QByteArray ba = qToUtf8(ctx->url);
        AVDictionary* opts = 0;
        std::string hdr;
        if (!ctx->net.referer.isEmpty())
            hdr += "Referer: " + std::string(qToUtf8(ctx->net.referer).data()) + "\r\n";
        if (!ctx->net.userAgent.isEmpty())
            hdr += "User-Agent: " + std::string(qToUtf8(ctx->net.userAgent).data()) + "\r\n";
        if (!hdr.empty()) av_dict_set(&opts, "headers", hdr.c_str(), 0);
        if (!ctx->net.proxy.isEmpty())
            av_dict_set(&opts, "http_proxy", qToUtf8(ctx->net.proxy).data(), 0);
        AVFormatContext* fmt = avformat_alloc_context();
        int rc = -1;
        if (fmt != 0) {
            fmt->interrupt_callback.callback = &CxFFmpegOpenThread::interruptCb;
            fmt->interrupt_callback.opaque = (void*)ctx->pAbort;
            rc = avformat_open_input(&fmt, ba.data(), 0, &opts);
            if (rc >= 0 && !ctx->cancel.load())
                rc = avformat_find_stream_info(fmt, 0);
        }
        if (opts) av_dict_free(&opts);
        if (ctx->cancel.load()) { if (fmt) avformat_close_input(&fmt); return; }
        {
            QMutexLocker locker(&ctx->mu);
            if (!ctx->sinkAlive || ctx->sink == 0) {
                if (fmt) avformat_close_input(&fmt);
                return;
            }
            QApplication::postEvent(ctx->sink, new CxFFmpegOpenEvent(fmt, rc, ctx->gen));
        }
    }
private:
    CxFFmpegOpenCtx* m_ctx;
};

// ── 引擎 ──

CXEngineFfmpeg::CXEngineFfmpeg()
    : CXEngine(), m_fmtCtx(0), m_vq(0), m_aq(0), m_sq(0), m_dec(0), m_adec(0),
      m_sdec(0), m_demux(0), m_vthread(0), m_athread(0), m_sthread(0),
      m_ao(0), m_clock(0),
      m_paused(false), m_running(false),
      m_durMs(0), m_curMs(0), m_vIdx(-1), m_aIdx(-1), m_sIdx(-1),
      m_seekPend(0), m_seekTarget(0), m_eosPend(0), m_fatalErr(false),
      m_aCount(0), m_sCount(0), m_audioPrompt(-1), m_subPrompt(-1),
      m_rateCur(1.0), m_volCur(100), m_muteCur(false),
      m_rotation(0), m_eosTimer(0)
{
}

CXEngineFfmpeg::~CXEngineFfmpeg()
{
    cancelOpen();
    close();
    QApplication::removePostedEvents(this);
}

#define FMT_CTX() ((AVFormatContext*)m_fmtCtx)

void CXEngineFfmpeg::resetOpenState(const QString& url)
{
    if (m_eosTimer != 0)
        m_eosTimer->stop();
    m_eosPend = 0;
    m_url = url;
    m_paused = false;
    m_running = false;
    m_curMs = 0;
    m_vIdx = -1;
    m_aIdx = -1;
    m_sIdx = -1;
    m_aCount = 0;
    m_sCount = 0;
    m_rateCur = 1.0;
    m_openState.ioAbort = false;   /* 每次新 open 前复位网络读中断标志 */
    m_fatalErr = false;            /* 新会话复位致命错误标记 */
    m_lastFrameImg = QImage();     /* 丢弃上一段遗留帧 */
}

bool CXEngineFfmpeg::open(const QString& url)
{
    cancelOpen();
    stopThreads();         // 换源即重建
    resetOpenState(url);
    m_openState.ready = false;
    m_openState.playPending = true;   // 同步 open 保持旧语义:open 即起播
    m_openState.startedOnce = false;
    m_openState.pendingSeekMs = -1;

    QByteArray ba = qToUtf8(url);
    AVFormatContext* fmt = 0;
    // 直链网络选项:防盗链头 + 代理(与 API 同出口;ffmpeg 由 av_dict 传入)
    AVDictionary* opts = 0;
    std::string hdr;
    if (!m_net.referer.isEmpty())
        hdr += "Referer: " + std::string(qToUtf8(m_net.referer).data()) + "\r\n";
    if (!m_net.userAgent.isEmpty())
        hdr += "User-Agent: " + std::string(qToUtf8(m_net.userAgent).data()) + "\r\n";
    if (!hdr.empty()) av_dict_set(&opts, "headers", hdr.c_str(), 0);
    if (!m_net.proxy.isEmpty())
        av_dict_set(&opts, "http_proxy",
                    qToUtf8(m_net.proxy).data(), 0);
    const int openRc = avformat_open_input(&fmt, ba.data(), 0, &opts);
    if (opts) av_dict_free(&opts);
    if (openRc < 0) {
        emit noticedError((int)CxErrOpenFail, m_url);
        return false;
    }
    m_fmtCtx = fmt;
    if (avformat_find_stream_info(fmt, 0) < 0) {
        emit noticedError((int)CxErrOpenFail, m_url);
        close();
        return false;
    }
    return finishOpen(fmt);
}

// 只做后段 setup(不碰网络);同步 open() 与异步 openAsync() 共用。
bool CXEngineFfmpeg::finishOpen(AVFormatContext* fmt)
{
    m_openState.ready = true;   /* 从这里起 play/seek/属性走 Ready 分支 */

    // M3-A 轨道枚举 + prompt 选流(m_audioPrompt/m_subPrompt:
    //  -1=默认首个;-2=关闭;n=按枚举序第 n 个)
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        AVMediaType t = fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && m_vIdx < 0)
            m_vIdx = (int)i;
        else if (t == AVMEDIA_TYPE_AUDIO) {
            if (m_audioPrompt == -2) { // 关音轨
                m_aCount++;
                continue;
            }
            if (m_audioPrompt == -1 || m_audioPrompt == m_aCount)
                m_aIdx = (int)i; // 首个或第 N 个(一个都不缺时命中)
            m_aCount++;
        } else if (t == AVMEDIA_TYPE_SUBTITLE) {
            if (cffSubCodecTextual((int)fmt->streams[i]->codecpar->codec_id)) {
                bool sel = false;
                if (m_subPrompt >= -1) {
                    if ((m_subPrompt == -1 || m_subPrompt == m_sCount) &&
                        m_sIdx < 0) {
                        m_sIdx = (int)i;
                        sel = true;
                    }
                }
                m_sCount++;
                (void)sel;
            }
        }
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
    m_sq = new CxPacketQueue(128);
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

    m_demux = new CxDemuxThread(fmt, m_vq, m_vIdx, m_aq, m_aIdx, m_sq, m_sIdx,
                                this);
    m_vthread = new CXFFmpegVideoThread(m_vq, m_dec, this);
    m_vthread->setRate(m_rateCur);
    m_frameSlot.clear();                   // 新一轮:丢弃可能残留的旧帧
    m_vthread->setFrameSlot(&m_frameSlot); // 帧入槽,取代逐帧 QImage 事件
    if (m_clock != 0)
        m_vthread->setClock(m_clock);
    if (m_ao != 0)
        m_vthread->setAo(m_ao);
    if (m_adec != 0 && m_ao != 0)
        m_athread = new CXFFmpegAudioThread(m_aq, m_adec, m_ao, m_clock, this);
    // M3-A 字幕线程(内嵌文本子流)
    m_sdec = 0;
    m_sthread = 0;
    m_subTl.clear();
    if (m_sIdx >= 0) {
        m_sdec = new CxFFSubDecoder();
        QString serr;
        if (!m_sdec->init(fmt->streams[m_sIdx], &serr)) {
            delete m_sdec;
            m_sdec = 0;
            m_sIdx = -1;
        }
    }
    m_demux->start();
    m_vthread->start();
    if (m_athread != 0)
        m_athread->start();
    if (m_sdec != 0) {
        m_sthread = new CXFFmpegSubThread(m_sq, m_sdec, &m_subTl, this);
        m_sthread->start();
    }
    m_running = true;
    /* 只等主流:有视频=视频(+音频真正排空),纯音频=音频;字幕不参与 */
    m_eosPend = eosPrimaryCount();

    /* 落地 Opening 期暂存的音量/静音(线程已起,走 Ready 分支) */
    if (m_athread != 0) {
        m_athread->setVolume(m_volCur);
        m_athread->setMute(m_muteCur);
    }

    if (aoFallback)
        emit noticedError((int)CxErrAudio, m_url); // 非致命:silent 续播

    // M7:采集播放信息(分辨率/codec/fps/码率),供顶栏与详情弹窗使用
    m_info = CxVideoInfo();
    m_info.url = m_url;
    const char* hw = hwName();
    if (hw != 0)
        m_info.hw = QString::fromLatin1(hw);
    m_info.bitrateKbps = fmt->bit_rate > 0 ? fmt->bit_rate / 1000 : 0;
    if (m_vIdx >= 0 && (unsigned)m_vIdx < fmt->nb_streams) {
        AVStream* vs = fmt->streams[m_vIdx];
        if (vs->avg_frame_rate.den > 0)
            m_info.fps = double(vs->avg_frame_rate.num) / vs->avg_frame_rate.den;
        if (m_info.bitrateKbps == 0 && vs->codecpar->bit_rate > 0)
            m_info.bitrateKbps = vs->codecpar->bit_rate / 1000;
    }
    if (m_dec != 0) {
        m_info.width = m_dec->width();
        m_info.height = m_dec->height();
        m_info.codec = m_dec->codecName();
    }
    emit videoInfoChanged();

    /* Opening 期暂存 seek:线程起来后应用一次 */
    if (m_openState.pendingSeekMs >= 0) {
        const long ps = m_openState.pendingSeekMs;
        m_openState.pendingSeekMs = -1;
        seek(ps);
    }
    /* 意图感知:一次 open 只发一次 started;暂停/仅加载发 paused */
    if (!m_openState.startedOnce) {
        if (m_paused) {
            if (m_vthread != 0) m_vthread->setPaused(true);
            if (m_athread != 0) m_athread->setPaused(true);
            if (m_ao != 0) m_ao->pause();
            emit paused();
        } else if (m_openState.playPending) {
            emit started();
        } else {
            if (m_vthread != 0) m_vthread->setPaused(true);
            if (m_athread != 0) m_athread->setPaused(true);
            if (m_ao != 0) m_ao->pause();
            m_paused = true;
            emit paused();
        }
        m_openState.startedOnce = true;
    }
    if (m_durMs > 0)
        emit durationChanged(m_durMs);
    return true;
}

bool CXEngineFfmpeg::openAsync(const QString& url)
{
    cancelOpen();
    stopThreads();
    resetOpenState(url);
    m_openState.ready = false;
    m_openState.playPending = false;   /* play() 会置位;load-only 保持暂停 */
    m_openState.startedOnce = false;
    m_openState.pendingSeekMs = -1;
    ++m_openState.gen;

    CxFFmpegOpenCtx* ctx = new CxFFmpegOpenCtx;
    ctx->url = url;
    ctx->net = m_net;                     /* 快照:工作线程不读引擎成员 */
    ctx->gen = m_openState.gen;
    ctx->sink = this;
    ctx->pAbort = &m_openState.ioAbort;   /* 回调 opaque 绑定引擎级稳定标志 */
    m_openState.ctx = ctx;
    m_openState.thread = new CxFFmpegOpenThread(ctx);
    m_openState.thread->start();
    return true;
}

void CXEngineFfmpeg::cancelOpen()
{
    ++m_openState.gen;               /* 让在途结果失效 */
    if (m_openState.ctx != 0) {
        m_openState.ctx->cancel = true;
        m_openState.ioAbort = true;  /* 中断阻塞中的 open;此时必无 live demux */
        {
            QMutexLocker locker(&m_openState.ctx->mu);
            m_openState.ctx->sinkAlive = false;
        }
    }
    if (m_openState.thread != 0) {
        if (m_openState.thread->wait(2000)) {
            delete m_openState.thread;
            m_openState.thread = 0;
            delete m_openState.ctx;
            m_openState.ctx = 0;
        } else {
            /* 停不下来:沿用项目「泄漏运行中线程」策略 */
            m_openState.thread = 0;
            m_openState.ctx = 0;
        }
    } else if (m_openState.ctx != 0) {
        delete m_openState.ctx;
        m_openState.ctx = 0;
    }
}

void CXEngineFfmpeg::stopThreads()
{
    if (m_eosTimer != 0)
        m_eosTimer->stop();   // 本轮作废,别让看门狗打到下一轮
    m_eosPend = 0;
    m_running = false;        // 停止后不再接受在途 EOS(避免误连播)
    // 1) 全员先请求停止;2) 唤醒全部队列——队列满时阻塞 push 是 demux
    //    停不下来的根因(消费者先停,队列塞满,demux 卡死在 push);
    //    阻塞中的 push/take 被唤醒后立即返回,各线程得以退出
    if (m_vthread != 0)
        m_vthread->requestStop();
    if (m_athread != 0)
        m_athread->requestStop();
    if (m_demux != 0)
        m_demux->requestStop();
    if (m_sthread != 0)
        m_sthread->requestStop();
    if (m_vq != 0)
        m_vq->wake();
    if (m_aq != 0)
        m_aq->wake();
    if (m_sq != 0)
        m_sq->wake();

    // 泄漏策略(M5-A3 起逐步完备):线程停不下来,则它引用的队列/解码器
    // 一并泄漏不 delete——运行中析构任何一环都会 abort/segv
    bool vLeak = false;
    if (m_vthread != 0) {
        m_vthread->requestStop();
        if (!m_vthread->wait(2000)) {
            vLeak = true; // 停不下来就别 delete(QThread 运行中析构=abort)
            m_vthread = 0;
        } else {
            delete m_vthread;
            m_vthread = 0;
        }
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
        if (!m_demux->wait(2000)) {
            m_demux = 0; // 停不下来就泄漏(同 v/a 线程;运行中析构=abort)
        } else {
            delete m_demux;
            m_demux = 0;
        }
    }
    bool sLeak = false;
    if (m_sthread != 0) {
        m_sthread->requestStop();
        if (!m_sthread->wait(2000)) {
            sLeak = true;
            m_sthread = 0;
        } else {
            delete m_sthread;
            m_sthread = 0;
        }
    }
    if (!vLeak && m_vq != 0) {
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
    if (!sLeak && m_sq != 0) {
        m_sq->wake();
        m_sq->clear();
        delete m_sq;
        m_sq = 0;
    }
    if (!vLeak && m_dec != 0) {
        ALOG_INFO("cx: dec close begin");
        m_dec->close();
        ALOG_INFO("cx: dec close done");
        delete m_dec;
        m_dec = 0;
    }
    if (!sLeak && m_sdec != 0) {
        ALOG_INFO("cx: sdec close begin");
        m_sdec->close();
        ALOG_INFO("cx: sdec close done");
        delete m_sdec;
        m_sdec = 0;
    }
    m_subTl.clear(); // 内嵌时间轴重建;外挂 m_subTlExt 保留(重启不清)
    if (!audioLeak) {
        if (m_adec != 0) {
            ALOG_INFO("cx: adec close begin");
            m_adec->close();
            ALOG_INFO("cx: adec close done");
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
    m_frameSlot.clear(); // 丢弃在途帧,避免换源后显示旧画面
    m_lastFrameImg = QImage(); // 停止/换源后不再用遗留帧重绘旋转
}

void CXEngineFfmpeg::close()
{
    cancelOpen();
    stopThreads();
    m_openState.ready = false;
    m_openState.pendingSeekMs = -1;
    m_url = QString();
    m_durMs = 0;
}

bool CXEngineFfmpeg::play()
{
    if (!m_openState.ready) {
        if (m_openState.ctx != 0) {   /* Opening:记住意图,finishOpen 时起播 */
            m_openState.playPending = true;
            m_paused = false;
            return true;
        }
        return false;
    }
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
        emit started(); // 暂停恢复:状态回 CxPlaying(初播由 open 发)
    }
    return true;
}

bool CXEngineFfmpeg::pause()
{
    if (!m_openState.ready) {
        if (m_openState.ctx != 0) {   /* Opening:记住意图,finishOpen 时保持暂停 */
            m_paused = true;
            m_openState.playPending = false;
            return true;
        }
        return false;
    }
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
        emit paused(); // UI 依赖此信号回 CxPaused(原先缺失)
    }
    return true;
}

bool CXEngineFfmpeg::stop()
{
    const bool active = (m_fmtCtx != 0) || (m_openState.ctx != 0);
    cancelOpen();
    stopThreads();
    m_openState.ready = false;
    if (!active)
        return false;
    emit stopped();
    return true;
}

bool CXEngineFfmpeg::seek(long ms)
{
    if (!m_openState.ready) {
        if (m_openState.ctx != 0) {   /* Opening:暂存,finishOpen 后应用一次 */
            m_openState.pendingSeekMs = (ms < 0 ? 0 : ms);
            return true;
        }
        return false;
    }
    if (m_fmtCtx == 0 || m_demux == 0)
        return false;
    const int sIdx = (m_vIdx >= 0) ? m_vIdx : m_aIdx; // 纯音频亦能 seek
    if (sIdx < 0)
        return false;
    /* 已收到部分主流 EOS:本轮消费线程已退出,再清队列只会让它僵死;
     * UI 此时也应在推进下一项,直接拒绝这次 seek。 */
    if (m_eosPend < eosPrimaryCount())
        return false;
    if (ms < 0) ms = 0;
    if (m_durMs > 1000 && ms > m_durMs - 1000) ms = m_durMs - 1000;
    /* 已有一轮 seek 在途:只更新最新目标,不重复清队列/广播位置。
     * 长按方向键时由 demux 合并,避免“清队列风暴”引发 A/V 互等。 */
    if (m_seekPend) {
        m_seekTarget = ms;
        m_curMs = ms;
        m_demux->requestSeek(ms, sIdx);
        return true;
    }
    m_curMs = ms;
    /* UI 先动,不等解码(首帧到位后 vthread 再投 SeekDone) */
    emit positionChanged(ms);
    m_seekPend = 1;
    m_seekTarget = ms;
    /* demux 可能正阻塞在"队列满 push":先腾位,再投 seek 请求 */
    if (m_vq != 0) m_vq->clear();
    if (m_aq != 0) m_aq->clear();
    if (m_sq != 0) m_sq->clear();
    m_demux->requestSeek(ms, sIdx);
    m_frameSlot.clear(); // seek:丢弃旧帧,等目标帧到位
    return true;
}

bool CXEngineFfmpeg::setRate(double r)
{
    if (r <= 0.0)
        r = 1.0;
    m_rateCur = r;
    if (!m_openState.ready) return m_openState.ctx != 0;   /* 先存,Ready 后应用 */
    if (m_fmtCtx == 0) return false;
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
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    m_volCur = pct;
    m_muteCur = false;
    if (!m_openState.ready) return m_openState.ctx != 0;
    if (m_athread == 0) return false;
    m_athread->setVolume(pct);
    return true;
}

bool CXEngineFfmpeg::setMute(bool on)
{
    m_muteCur = on;
    if (!m_openState.ready) return m_openState.ctx != 0;
    if (m_athread == 0) return false;
    m_athread->setMute(on);
    return true;
}

void CXEngineFfmpeg::setRotation(int deg)
{
    deg = ((deg % 360) + 360) % 360;
    if (deg != 0 && deg != 90 && deg != 180 && deg != 270)
        deg = 0;
    m_rotation = deg;
    /* 暂停/静止:立即以未旋转源重绘最近一帧 */
    if (p_renderer != 0 && !m_lastFrameImg.isNull()) {
        CXVideoFrame rf;
        rf.ptsMs = m_curMs;
        rf.img = cxRotateRgb32(m_lastFrameImg, m_rotation);
        rf.width = rf.img.width();
        rf.height = rf.img.height();
        p_renderer->showFrame(&rf);
    }
}

const char* CXEngineFfmpeg::aoBackend() const
{
    return m_ao != 0 ? m_ao->backend() : 0;
}

// ── M3-A:轨道 + 字幕 ──

// 换轨重启:态保留(rate/vol/mute/外挂字幕/当前位置);wasPlaying=false 则暂停续位
bool CXEngineFfmpeg::reopenAt(long posMs, bool wasPlaying)
{
    const QString url = m_url;
    if (url.isEmpty())
        return false;
    const double rate = m_rateCur;   // openAsync 会复位默认值,先存用户意图
    const int vol = m_volCur;
    const bool mute = m_muteCur;
    m_subCur = QString();
    if (!openAsync(url))
        return false;
    m_rateCur = rate;
    m_volCur = vol;
    m_muteCur = mute;
    m_openState.pendingSeekMs = (posMs > 0) ? posMs : -1;
    m_paused = !wasPlaying;
    m_openState.playPending = wasPlaying;
    return true;
}

bool CXEngineFfmpeg::setAudioTrack(int idx)
{
    if (m_fmtCtx == 0) {
        if (!m_openState.ready && m_openState.ctx != 0) {   /* Opening:先记意图 */
            m_audioPrompt = (idx == -1) ? -2 : idx;
            return true;
        }
        return false;
    }
    int curOrd = (m_audioPrompt == -2) ? -1 :
                 (m_audioPrompt == -1 ? 0 : m_audioPrompt);
    if (idx == -1) {
        if (curOrd == -1)
            return false; // 已关
        m_audioPrompt = -2;
    } else {
        if (m_aCount <= 0 || idx >= m_aCount)
            return false;
        if (curOrd == idx)
            return false; // 幂等:正在此轨
        m_audioPrompt = idx;
    }
    return reopenAt(m_curMs, !m_paused);
}

bool CXEngineFfmpeg::setSubtitleTrack(int idx)
{
    if (m_fmtCtx == 0) {
        if (!m_openState.ready && m_openState.ctx != 0) {   /* Opening:先记意图 */
            m_subPrompt = (idx == -1) ? -2 : idx;
            return true;
        }
        return false;
    }
    int curOrd = (m_subPrompt == -2) ? -1 : m_subPrompt;
    if (idx == -1) {
        if (curOrd == -1)
            return false; // 已关(内嵌)
        m_subPrompt = -2;
    } else {
        if (m_sCount <= 0 || idx >= m_sCount)
            return false;
        if (curOrd == idx)
            return false; // 幂等:正在此轨
        if (idx == 0 && m_subPrompt == -1) {
            // 默认已是首轨(自动选中):仅固化选态,不重建管线
            m_subPrompt = 0;
            return true;
        }
        m_subPrompt = idx;
    }
    return reopenAt(m_curMs, !m_paused);
}

bool CXEngineFfmpeg::setSubtitleFile(const QString& path)
{
    m_subTlExt.clear(); // cffParseSrt 打不开文件即返回 false,途中不追加
    if (!path.isEmpty()) {
        if (!cffParseSrt(path, &m_subTlExt)) {
            m_subFile = QString();
            cxScanSub(m_curMs);
            emit noticedError((int)CxErrOpenFail, path);
            return false;
        }
        m_subFile = path;
    } else {
        m_subFile = QString();
    }
    cxScanSub(m_curMs);
    return true;
}

void CXEngineFfmpeg::cxScanSub(long ms)
{
    QString t;
    bool hit = m_subTlExt.lookup(ms, t);
    if (!hit)
        hit = m_subTl.lookup(ms, t);
    QString cur = hit ? t : QString();
    if (cur != m_subCur) {
        m_subCur = cur;
        emit subtitleChanged(cur);
    }
}

long CXEngineFfmpeg::position() const
{
    return m_curMs;
}

long CXEngineFfmpeg::duration() const
{
    return m_durMs;
}

long CXEngineFfmpeg::bufferedMs() const
{
    return m_demux != 0 ? m_demux->bufferedMs() : 0;
}

CxBufferInfo CXEngineFfmpeg::bufferInfo() const
{
    CxBufferInfo info;
    info.bufferedMs = bufferedMs();
    info.maxBytes = CX_BUFFER_MAX_BYTES_DEFAULT;
    if (m_demux != 0) {
        info.bufferedBytes = m_demux->bufferedBytes();
        info.bytesPerSec = m_demux->bytesPerSec();
    }
    if (info.bufferedBytes < 0)
        info.bufferedBytes = 0;
    if (info.bytesPerSec < 0)
        info.bytesPerSec = 0;
    return info;
}

void CXEngineFfmpeg::customEvent(CustomEventBase* e)
{
    if (e->type() == toEventType34(kcxFFmpegOpenEventRaw)) {
        CxFFmpegOpenEvent* oe = (CxFFmpegOpenEvent*)e;
        AVFormatContext* fmt = (AVFormatContext*)oe->fmt;
        oe->fmt = 0;
        const int gen = oe->gen;
        const int rc = oe->rc;

        /* 仅当代际匹配时,m_openState.thread/ctx 才属于本次;否则不动新任务 */
        if (gen == m_openState.gen) {
            if (m_openState.thread != 0) {
                m_openState.thread->wait(1000);
                delete m_openState.thread;
                m_openState.thread = 0;
            }
            if (m_openState.ctx != 0) {
                delete m_openState.ctx;
                m_openState.ctx = 0;
            }
        }
        if (gen != m_openState.gen) {   /* 旧代:静默释放,不报错、不起播 */
            if (fmt) avformat_close_input(&fmt);
            return;
        }
        if (rc < 0 || fmt == 0) {
            if (fmt) avformat_close_input(&fmt);
            m_openState.ready = false;
            emit noticedError((int)CxErrOpenFail, m_url);
            return;
        }
        m_fmtCtx = fmt;
        finishOpen(fmt);
        return;
    }
    if (e->type() != toEventType34(kcxFFmpegEventRaw))
        return;
    dispatchEvent((CxFFmpegUiEvent*)e);
}

void CXEngineFfmpeg::dispatchEvent(const CxFFmpegUiEvent* ev)
{
    switch (ev->ev) {
    // [可清理] 旧逐帧事件处理:正式路径已切到 CxFFmpegEvFrameReady;
    // 引擎恒装配帧槽,此 case 实际不可达,稳定后可整段移除。
    case CxFFmpegEvFrame: {
        if (ev->frame != 0) {
            /* seek 期间:只接受目标 ±2s 内的帧事件。队列里可能残留 seek 前
             * 投递的旧帧(前进/后退两个方向),会造成进度条短暂回跳/跳变。
             * 帧随事件析构释放(~CxFFmpegUiEvent)。 */
            if (m_seekPend && (ev->ms > m_seekTarget + 2000
                               || ev->ms + 2000 < m_seekTarget))
                break;
            m_curMs = ev->ms;
            if (p_renderer != 0)
                p_renderer->showFrame(ev->frame);
            emit positionChanged(ev->ms);
            cxScanSub(ev->ms);
        }
        break;
    }
    case CxFFmpegEvFrameReady: {
        /* 视频线程只投轻量"有新帧"通知,帧本体在 m_frameSlot(恒定一帧)。
         * 取帧后按自身 pts 做 seek 过滤,UI 负责释放(事件不持有帧)。 */
        CXVideoFrame* f = m_frameSlot.take();
        if (f == 0)
            break;
        if (m_seekPend && (f->ptsMs > m_seekTarget + 2000
                           || f->ptsMs + 2000 < m_seekTarget)) {
            delete f;
            break;
        }
        m_curMs = f->ptsMs;
        /* 保存未旋转源,供暂停时改旋转立即重绘 */
        m_lastFrameImg = f->img;
        if (m_rotation != 0) {
            CXVideoFrame rf;
            rf.ptsMs = f->ptsMs;
            rf.isKey = f->isKey;
            rf.img = cxRotateRgb32(f->img, m_rotation);
            rf.width = rf.img.width();
            rf.height = rf.img.height();
            if (p_renderer != 0)
                p_renderer->showFrame(&rf);
        } else if (p_renderer != 0) {
            p_renderer->showFrame(f);
        }
        emit positionChanged(f->ptsMs);
        cxScanSub(f->ptsMs);
        delete f; // FrameReady 事件 frame 字段为空,此处释放取出的帧
        break;
    }
    case CxFFmpegEvFinished: {
        if (!m_running)
            break;
        /* code: 1=video 2=audio 3=sub。只统计主流,两者(或唯一主流)
         * 都 drain 完才收尾;先到的先记数,并起看门狗等后到的。 */
        const int role = ev->code;
        const bool primary = (m_vIdx >= 0) ? (role == 1) : (role == 2);
        if (!primary)
            break;
        if (m_eosPend > 0)
            --m_eosPend;
        if (m_eosPend > 0) {
            if (m_eosTimer == 0) {
                m_eosTimer = new QTimer(this);
                QObject::connect(m_eosTimer, SIGNAL(timeout()),
                                 this, SLOT(onEosWatchdog()));
            }
            m_eosTimer->start(3000);
            break;
        }
        finishEos();
        break;
    }
    case CxFFmpegEvSeekDone:
        if (m_seekPend) {
            m_seekPend = 0;
            m_seekTarget = 0;
            emit seekFinished(ev->ms);
        }
        break;
    case CxFFmpegEvAudioPos:
        // 视频缺席(纯音频)时 position 由音频时钟驱动;有视频时以帧为准
        if (m_vIdx < 0) {
            m_curMs = ev->ms;
            emit positionChanged(ev->ms);
            cxScanSub(ev->ms);
        }
        break;
    case CxFFmpegEvError:
        if (ev->code != (int)CxErrSeek) { /* seek 超界非致命;其它视为致命读错误 */
            m_fatalErr = true;
            if (m_eosTimer != 0)
                m_eosTimer->stop();
            m_eosPend = 0;                /* 后续 EOS 由 finishEos 吞掉 */
        }
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

/* 本次会话需要等待 EOS 的主流数量:有视频则视频+音频,纯音频则仅音频 */
int CXEngineFfmpeg::eosPrimaryCount() const
{
    int n = 0;
    if (m_vIdx >= 0 && m_vthread != 0)
        n++;
    if (m_athread != 0)
        n++;
    return n > 0 ? n : 1;   /* 兜底:任何会话都要有一个事件能收尾 */
}

void CXEngineFfmpeg::finishEos()
{
    if (m_eosTimer != 0)
        m_eosTimer->stop();
    m_eosPend = 0;
    if (!m_running)
        return;
    m_running = false;
    if (m_fatalErr)     /* 致命读错误已走 skipOnError,避免双重推进 */
        return;
    if (!m_subCur.isEmpty()) { // 收尾清屏
        m_subCur = QString();
        emit subtitleChanged(QString());
    }
    emit finished();
}

void CXEngineFfmpeg::onEosWatchdog()
{
    /* AO drain 卡死兜底:宁可少等,不能让连播永久停住 */
    finishEos();
}
