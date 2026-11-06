#include "cxffdec.h"
#include "cxframe.h"
#include <stdio.h>

CxDecoder::CxDecoder()
    : m_ctx(0), m_stream(0), m_frame(0), m_out(0), m_sws(0),
      m_swsW(-1), m_swsH(-1), m_swsPix(-1), m_flushed(false)
{
}

CxDecoder::~CxDecoder()
{
    close();
}

bool CxDecoder::init(AVStream* s, QString* err)
{
    m_stream = s;
    if (s == 0 || s->codecpar == 0) {
        if (err) *err = QString("no video stream");
        return false;
    }
    const AVCodec* dec = avcodec_find_decoder(s->codecpar->codec_id);
    if (dec == 0) {
        if (err) *err = QString("decoder not found");
        return false;
    }
    m_ctx = avcodec_alloc_context3(dec);
    if (m_ctx == 0) {
        if (err) *err = QString("alloc ctx failed");
        return false;
    }
    avcodec_parameters_to_context(m_ctx, s->codecpar);
    m_ctx->pkt_timebase = s->time_base; // FFmpeg 7: pts 换算必要
    if (avcodec_open2(m_ctx, dec, 0) < 0) {
        if (err) *err = QString("open decoder failed");
        close();
        return false;
    }
    m_codecName = QString::fromLatin1(dec->name);
    m_frame = av_frame_alloc();
    if (m_frame == 0) {
        if (err) *err = QString("alloc frame failed");
        close();
        return false;
    }
    return true;
}

void CxDecoder::flush()
{
    if (m_ctx != 0)
        avcodec_flush_buffers(m_ctx);
    if (m_out != 0) {
        av_frame_free(&m_out);
        m_out = 0;
    }
    for (size_t i = 0; i < m_stash.size(); i++)
        av_frame_free(&m_stash[i]);
    m_stash.clear();
    m_flushed = false;
}

void CxDecoder::close()
{
    if (m_sws != 0) {
        sws_freeContext(m_sws);
        m_sws = 0;
    }
    m_swsW = m_swsH = m_swsPix = -1;
    if (m_out != 0) {
        av_frame_free(&m_out);
        m_out = 0;
    }
    for (size_t i = 0; i < m_stash.size(); i++)
        av_frame_free(&m_stash[i]);
    m_stash.clear();
    if (m_frame != 0)
        av_frame_free(&m_frame);
    if (m_ctx != 0)
        avcodec_free_context(&m_ctx);
    m_stream = 0;
}

AVFrame* CxDecoder::decode(AVPacket* pkt)
{
    if (m_ctx == 0)
        return 0;
    if (m_out != 0) { // 回收上一次出借的帧
        av_frame_free(&m_out);
        m_out = 0;
    }
    if (!m_stash.empty()) { // 余帧优先
        m_out = m_stash.front();
        m_stash.pop_front();
        return m_out;
    }
    if (pkt != 0) {
        if (m_flushed)
            m_flushed = false; // 正常新包恢复
        if (avcodec_send_packet(m_ctx, pkt) < 0)
            return 0;
    } else {
        // 尾部 drain(仅一次;重复调用立即 0)
        if (m_flushed)
            return 0;
        avcodec_send_packet(m_ctx, 0);
        m_flushed = true;
    }
    while (true) {
        int r = avcodec_receive_frame(m_ctx, m_frame);
        if (r == 0) {
            AVFrame* c = av_frame_clone(m_frame);
            if (c != 0)
                m_stash.push_back(c);
            continue;
        }
        break; // EAGAIN / EOF / 错误
    }
    if (!m_stash.empty()) {
        m_out = m_stash.front();
        m_stash.pop_front();
        return m_out;
    }
    return 0;
}

QImage CxDecoder::convert(AVFrame* avf)
{
    if (avf == 0 || avf->width <= 0 || avf->height <= 0)
        return QImage();
    int pix = avf->format;
    if (m_sws == 0 || m_swsW != avf->width || m_swsH != avf->height ||
        m_swsPix != pix) {
        m_sws = sws_getCachedContext(m_sws, avf->width, avf->height,
                                     (AVPixelFormat)pix,
                                     avf->width, avf->height,
                                     AV_PIX_FMT_RGB32, SWS_BILINEAR,
                                     0, 0, 0);
        if (m_sws == 0)
            return QImage();
        m_swsW = avf->width;
        m_swsH = avf->height;
        m_swsPix = pix;
    }
    QImage img = cxNewImage(avf->width, avf->height);
    if (img.isNull())
        return QImage();
    uint8_t* dst[4] = { 0, 0, 0, 0 };
    int dstLine[4] = { 0, 0, 0, 0 };
    dst[0] = img.bits();
    dstLine[0] = img.bytesPerLine();
    sws_scale(m_sws, (const uint8_t* const*)avf->data, avf->linesize, 0,
              avf->height, dst, dstLine);
    return img;
}

static long cxTimeToMs(long long pts, AVRational tb)
{
    if (pts == AV_NOPTS_VALUE)
        return -1;
    if (tb.den == 0)
        return -1;
    return (long)(av_rescale_q(pts, tb, AV_TIME_BASE_Q) / 1000);
}

long CxDecoder::framePtsMs(AVFrame* avf) const
{
    if (avf == 0)
        return -1;
    // FFmpeg 7: send 时设 pkt_timebase,receive 出的 pts 即该基
    AVRational tb = (m_ctx != 0) ? m_ctx->pkt_timebase : AVRational();
    if (tb.den == 0 && m_stream != 0)
        tb = m_stream->time_base;
    long long pts = avf->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = avf->pts;
    return cxTimeToMs(pts, tb);
}

long CxDecoder::frameDurationMs(AVFrame* avf) const
{
    if (avf != 0 && avf->duration > 0 && m_stream != 0 &&
        m_stream->time_base.den != 0)
        return (long)(av_rescale_q(avf->duration, m_stream->time_base,
                                   AV_TIME_BASE_Q) / 1000);
    double fps = 0.0;
    if (m_stream != 0 && m_stream->r_frame_rate.den != 0)
        fps = av_q2d(m_stream->r_frame_rate);
    if (fps <= 0.0)
        fps = 25.0;
    return (long)(1000.0 / fps + 0.5);
}
