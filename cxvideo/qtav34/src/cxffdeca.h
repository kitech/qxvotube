#ifndef CXFFDECA_H
#define CXFFDECA_H

#include "cxff_wrap.h"
#include "cxvideo_global.h"
#include <qstring.h>
#include <deque>

// 音频解码 + swresample 重采样(FFmpeg 7:AVChannelLayout/swr_alloc_set_opts2;
// 网搜核对:swr_get_out_samples 上界、convert(NULL,0) 冲洗、AAC priming 由
// libavcodec decode 自动丢弃 skip_samples;残留负 pts 由 athread 钳 0)
// decode 契约同 CxDecoder:返回帧仅到下一次 decode/flush/close 有效
class CxDecoderAudio {
public:
    CxDecoderAudio();
    ~CxDecoderAudio();

    bool init(AVStream* s, QString* err = 0);
    void flush();                    // avcodec flush + stash 清 + swr 重建
    void close();

    AVFrame* decode(AVPacket* pkt);  // pkt=0 → 尾部 drain

    // 重采样到交错 S16(outCh/outRate = AO obtained,athread 传入);
    // 写入内部 arena,每次调用覆盖;convertFlush 冲洗残余
    bool convert(AVFrame* in, int outCh, int outRate);
    bool convertFlush();

    const uint8_t* pcmData() const { return m_pcm; }
    int pcmBytes() const { return m_pcmBytes; }
    int pcmSamples() const { return m_pcmSamples; }

    long framePtsMs(AVFrame* avf) const;

    const QString& codecName() const { return m_codecName; }
    int inRate() const { return m_ctx != 0 ? m_ctx->sample_rate : 0; }
    int inCh() const
    {
        return (m_ctx != 0 && m_ctx->ch_layout.nb_channels > 0) ?
                   m_ctx->ch_layout.nb_channels : 2;
    }

private:
    bool ensureSwr(AVFrame* in, int outCh, int outRate);
    bool growArena(int needBytes);
    void clearPcm();

    AVCodecContext* m_ctx;
    AVStream* m_stream;
    AVFrame* m_frame;
    AVFrame* m_out;
    SwrContext* m_swr;
    int m_swrCh;
    int m_swrRate;
    int m_swrInFmt;
    int m_swrInRate;
    QString m_codecName;
    std::deque<AVFrame*> m_stash;

    uint8_t* m_pcm;
    int m_pcmCap;
    int m_pcmBytes;
    int m_pcmSamples;
};

#endif // CXFFDECA_H
