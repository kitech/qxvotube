#include "cxffdeca.h"

CxDecoderAudio::CxDecoderAudio()
    : m_ctx(0), m_stream(0), m_frame(0), m_out(0), m_swr(0),
      m_swrCh(0), m_swrRate(0), m_swrInFmt(-1), m_swrInRate(0),
      m_pcm(0), m_pcmCap(0), m_pcmBytes(0), m_pcmSamples(0)
{
}

CxDecoderAudio::~CxDecoderAudio()
{
    close();
}

bool CxDecoderAudio::init(AVStream* s, QString* err)
{
    m_stream = s;
    if (s == 0 || s->codecpar == 0 ||
        s->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        if (err) *err = QString("no audio stream");
        return false;
    }
    const AVCodec* dec = avcodec_find_decoder(s->codecpar->codec_id);
    if (dec == 0) {
        if (err) *err = QString("audio decoder not found");
        return false;
    }
    m_ctx = avcodec_alloc_context3(dec);
    if (m_ctx == 0) {
        if (err) *err = QString("alloc ctx failed");
        return false;
    }
    avcodec_parameters_to_context(m_ctx, s->codecpar);
    m_ctx->pkt_timebase = s->time_base;
    if (avcodec_open2(m_ctx, dec, 0) < 0) {
        if (err) *err = QString("open audio decoder failed");
        close();
        return false;
    }
    m_frame = av_frame_alloc();
    if (m_frame == 0) {
        if (err) *err = QString("alloc frame failed");
        close();
        return false;
    }
    m_codecName = QString::fromLatin1(dec->name);
    return true;
}

void CxDecoderAudio::flush()
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
    // swr 间歇态作废(seek):下次 convert 重建
    if (m_swr != 0) {
        swr_free(&m_swr);
        m_swr = 0;
    }
    m_swrCh = m_swrRate = 0;
    m_swrInFmt = -1;
    m_swrInRate = 0;
    clearPcm();
}

void CxDecoderAudio::close()
{
    if (m_swr != 0) {
        swr_free(&m_swr);
        m_swr = 0;
    }
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
    if (m_pcm != 0) {
        av_free(m_pcm);
        m_pcm = 0;
    }
    m_pcmCap = m_pcmBytes = m_pcmSamples = 0;
    m_stream = 0;
}

void CxDecoderAudio::clearPcm()
{
    m_pcmBytes = m_pcmSamples = 0;
}

AVFrame* CxDecoderAudio::decode(AVPacket* pkt)
{
    if (m_ctx == 0)
        return 0;
    if (m_out != 0) { // 回收上一次出借的帧
        av_frame_free(&m_out);
        m_out = 0;
    }
    if (!m_stash.empty()) {
        m_out = m_stash.front();
        m_stash.pop_front();
        return m_out;
    }
    if (pkt != 0) {
        if (avcodec_send_packet(m_ctx, pkt) < 0)
            return 0;
    } else {
        avcodec_send_packet(m_ctx, 0); // 尾部 drain
    }
    while (true) {
        int r = avcodec_receive_frame(m_ctx, m_frame);
        if (r == 0) {
            AVFrame* c = av_frame_clone(m_frame);
            if (c != 0)
                m_stash.push_back(c);
            continue;
        }
        break;
    }
    if (!m_stash.empty()) {
        m_out = m_stash.front();
        m_stash.pop_front();
        return m_out;
    }
    return 0;
}

bool CxDecoderAudio::growArena(int needBytes)
{
    if (m_pcmCap >= needBytes)
        return true;
    if (m_pcm != 0)
        av_free(m_pcm);
    m_pcm = (uint8_t*)av_malloc((size_t)needBytes);
    if (m_pcm == 0) {
        m_pcmCap = 0;
        return false;
    }
    m_pcmCap = needBytes;
    return true;
}

bool CxDecoderAudio::ensureSwr(AVFrame* in, int outCh, int outRate)
{
    if (in == 0 || outCh <= 0 || outRate <= 0)
        return false;
    const AVChannelLayout* inLayout = &in->ch_layout;
    if (inLayout->nb_channels <= 0 && m_ctx != 0)
        inLayout = &m_ctx->ch_layout;
    int inRate = in->sample_rate > 0 ? in->sample_rate :
                 (m_ctx != 0 ? m_ctx->sample_rate : 0);
    if (m_swr != 0 && m_swrCh == outCh && m_swrRate == outRate &&
        m_swrInFmt == in->format && inRate == m_swrInRate)
        return true;
    if (m_swr != 0) {
        swr_free(&m_swr);
        m_swr = 0;
    }
    AVChannelLayout il, ol;
    if (av_channel_layout_copy(&il, inLayout) < 0)
        return false;
    av_channel_layout_default(&ol, outCh);
    int r = swr_alloc_set_opts2(&m_swr, &ol, AV_SAMPLE_FMT_S16, outRate,
                                &il, (AVSampleFormat)in->format,
                                inRate, 0, 0);
    av_channel_layout_uninit(&il);
    av_channel_layout_uninit(&ol);
    if (r < 0)
        return false;
    if (swr_init(m_swr) < 0) {
        swr_free(&m_swr);
        m_swr = 0;
        return false;
    }
    m_swrCh = outCh;
    m_swrRate = outRate;
    m_swrInFmt = in->format;
    m_swrInRate = inRate;
    return true;
}

bool CxDecoderAudio::convert(AVFrame* in, int outCh, int outRate)
{
    if (!ensureSwr(in, outCh, outRate))
        return false;
    int up = swr_get_out_samples(m_swr, in->nb_samples);
    if (up < in->nb_samples)
        up = in->nb_samples;
    int need = up * outCh * 2;
    if (!growArena(need > 0 ? need : 8192))
        return false;
    uint8_t* dst = m_pcm;
    int got = swr_convert(m_swr, &dst, up,
                          (const uint8_t**)in->data, in->nb_samples);
    m_pcmSamples = got > 0 ? got : 0;
    m_pcmBytes = m_pcmSamples * outCh * 2;
    return m_pcmBytes > 0;
}

bool CxDecoderAudio::convertFlush()
{
    if (m_swr == 0)
        return false;
    int up = swr_get_out_samples(m_swr, 0);
    if (up <= 0)
        return false;
    if (!growArena(up * m_swrCh * 2))
        return false;
    uint8_t* dst = m_pcm;
    int got = swr_convert(m_swr, &dst, up, 0, 0); // NULL,0 冲洗
    m_pcmSamples = got > 0 ? got : 0;
    m_pcmBytes = m_pcmSamples * m_swrCh * 2;
    return m_pcmBytes > 0;
}

static long cxAudioTimeToMs(long long pts, AVRational tb)
{
    if (pts == AV_NOPTS_VALUE)
        return -1;
    if (tb.den == 0)
        return -1;
    return (long)(av_rescale_q(pts, tb, AV_TIME_BASE_Q) / 1000);
}

long CxDecoderAudio::framePtsMs(AVFrame* avf) const
{
    if (avf == 0)
        return -1;
    AVRational tb = (m_ctx != 0) ? m_ctx->pkt_timebase : AVRational();
    if (tb.den == 0 && m_stream != 0)
        tb = m_stream->time_base;
    long long pts = avf->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = avf->pts;
    return cxAudioTimeToMs(pts, tb);
}
