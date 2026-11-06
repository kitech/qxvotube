#ifndef CXFFDEC_H
#define CXFFDEC_H

#include "cxff_wrap.h"
#include "cxvideo_global.h"
#include <qstring.h>
#include <qimage.h>
#include <deque>

// 视频解码 + sws→RGB32 QImage(detail.md §5;"sws 上下文一次缓存")
// 所有权契约:decode() 返回的 AVFrame 在下一次 decode()/flush()/close() 前有效
class CxDecoder {
public:
    CxDecoder();
    ~CxDecoder();

    // stream 提供解码器与时间基;失败返回 false(reason 填 err)
    bool init(AVStream* s, QString* err = 0);
    void flush();
    void close();

    // 解码一个包(pkt 由调用方 free);无帧返回 0。
    // pkt=0 → 尾部 drain(仅 avformat 输完 EOS 后调,直到 0)
    AVFrame* decode(AVPacket* pkt);

    // convert(avf) → RGB32 QImage;sws ctx 按 (w,h,pix) 缓存
    QImage convert(AVFrame* avf);

    long framePtsMs(AVFrame* avf) const;
    long frameDurationMs(AVFrame* avf) const;

    const QString& codecName() const { return m_codecName; }
    int width() const { return m_ctx ? m_ctx->width : 0; }
    int height() const { return m_ctx ? m_ctx->height : 0; }

private:
    AVCodecContext* m_ctx;
    AVStream* m_stream;
    AVFrame* m_frame;      // receive 复用帧
    AVFrame* m_out;        // 交给调用方的当期帧
    SwsContext* m_sws;
    int m_swsW, m_swsH, m_swsPix;
    bool m_flushed;
    std::deque<AVFrame*> m_stash;
    QString m_codecName;
};

#endif // CXFFDEC_H
