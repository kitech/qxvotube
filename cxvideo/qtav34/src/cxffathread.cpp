#include "cxffathread.h"
#include <qapplication.h>
#include <string.h>

// M3-B splice crossfade 长度(样本帧):128 ≈2.9ms @44.1k
static const int CxAF_NS = 128;

void CXFFmpegAudioThread::run()
{
    while (!stopFlag) {
        waitIfPaused();
        if (stopFlag)
            break;
        CxPkt p = aQueue->take();
        if (p.pkt == 0 && !p.eos)
            continue;
        if (p.eos) {
            while (!stopFlag) {
                AVFrame* fr = dec->decode(0);
                if (fr == 0)
                    break;
                deliver(fr);
            }
            tailFlush();
            if (!stopFlag && out != 0)
                out->drain(); // QtAV 五要点之四:EOF 先排空再退
            post(CxFFmpegEvFinished, 0); // 引擎 m_running 防重
            break;
        }
        AVFrame* fr = dec->decode(p.pkt);
        if (p.pkt != 0)
            av_packet_free(&p.pkt);
        if (fr != 0)
            deliver(fr);
    }
    if (m_blkBuf != 0) {
        delete[] m_blkBuf;
        m_blkBuf = 0;
    }
}

void CXFFmpegAudioThread::waitIfPaused()
{
    QMutexLocker lk(&paceMx);
    while (paused && !stopFlag)
        paceCond.wait(&paceMx, 120);
}

void CXFFmpegAudioThread::deliver(AVFrame* frame)
{
    long pts = dec->framePtsMs(frame);
    if (pts < 0)
        pts = 0; // AAC priming 残余负 pts 钳 0
    if (renderPts0 >= 0 && pts < renderPts0)
        return;
    bool seekReached = (renderPts0 >= 0 && pts >= renderPts0);
    if (seekReached)
        renderPts0 = -1;

    if (out == 0 || out->outCh() <= 0 || out->outRate() <= 0)
        return;
    if (!dec->convert(frame, out->outCh(), out->outRate()))
        return;
    int aCh = out->outCh();
    int aRate = out->outRate();
    if (dec->pcmBytes() > 0) {
        if (!writePcmChunks(dec->pcmData(), dec->pcmBytes(), pts, aCh, aRate))
            return;
    }
    if (seekReached)
        post(CxFFmpegEvSeekDone, pts);
}

bool CXFFmpegAudioThread::writePcmChunks(const uint8_t* src, int bytes,
                                         long baseMs, int aCh, int aRate)
{
    int bpf = aCh * 2; // s16 交错 / 样本帧
    long samples = (long)bytes / bpf;
    int chunkSamples = aRate * 40 / 1000; // ≈40ms 块
    if (chunkSamples < 512)
        chunkSamples = 512;
    if (chunkSamples > (int)samples && samples > 0)
        chunkSamples = (int)samples;
    int chunkBytes = chunkSamples * bpf;
    int off = 0;
    bool prevSkip = false; // 上一块被弃 → 本块头渐入
    while (off + chunkBytes <= bytes) {
        // 倍速补偿坐在 chunk 边界:
        //  rate < 1   → 复写 (1-rate)*samples 块粒度 dupAcc 累计
        //  rate > 1   → 丢弃 (rate-1)*samples 块粒度 skipAcc 累计
        // 每块仅 push 一次时钟(与实际新责 RTP 时长无关,密度一致)
        bool doWrite = true;
        bool doDup = false;
        bool skipNext = false;
        if (rate > 1.0) {
            // 先探:下一块将被弃 → 本块尾渐出(消除拼接点击;与原判等价)
            if (m_skipAcc + (rate - 1.0) * chunkSamples >= (double)chunkSamples)
                skipNext = true;
            m_skipAcc += (rate - 1.0) * chunkSamples;
            if (m_skipAcc >= chunkSamples) {
                m_skipAcc -= chunkSamples;
                doWrite = false;
            }
        } else if (rate < 1.0) {
            m_dupAcc += (1.0 - rate) * chunkSamples;
            if (m_dupAcc >= chunkSamples) {
                m_dupAcc -= chunkSamples;
                doDup = true;
            }
        }
        if (doWrite) {
            if (prevSkip || skipNext) {
                // M3-B crossfade:头/尾 128 帧线性斜坡,拼接去咔哒
                if (!cxFadeAndWrite(src + off, chunkBytes, bpf,
                                    prevSkip, skipNext))
                    return false;
            } else if (!writeScaled(src + off, chunkBytes))
                return false;
        }
        if (doDup && !writeScaled(src + off, chunkBytes))
            return false;
        off += chunkBytes;
        prevSkip = !doWrite;
        long endMs = baseMs +
            (long)((double)(off / bpf) * 1000.0 / aRate);
        m_clock->updateValue(endMs);
        post(CxFFmpegEvAudioPos, endMs);
    }
    int rem = bytes - off;
    if (rem > 0 && !stopFlag) {
        if (!writeScaled(src + off, rem))
            return false;
        off += rem;
    }
    m_lastMs = baseMs + (long)((double)(off / bpf) * 1000.0 / aRate);
    return !stopFlag;
}

// M3-B splice crossfade:头/尾各 128 帧线性斜坡(≈2.9ms @44.1k;s16 全声道
// 一起缓变);调用方仅在被弃块拼接边界走此路径,主体 writeScaled 音量路照旧
bool CXFFmpegAudioThread::cxFadeAndWrite(const uint8_t* src, int n, int bpf,
                                         bool head, bool tail)
{
    ensureBuf(n);
    memcpy(m_blkBuf, src, n);
    int16_t* d = (int16_t*)m_blkBuf;
    int ch = bpf / 2;         // 通道数(s16)
    int ns = n / bpf;         // 样本帧数
    if (head && ns > CxAF_NS) {
        for (int i = 0; i < CxAF_NS; i++)
            for (int c = 0; c < ch; c++) {
                long idx = (long)i * ch + c;
                d[idx] = (int16_t)((long)d[idx] * i / CxAF_NS);
            }
    }
    if (tail && ns > CxAF_NS) {
        for (int i = 0; i < CxAF_NS; i++)
            for (int c = 0; c < ch; c++) {
                long idx = (long)(ns - CxAF_NS + i) * ch + c;
                d[idx] = (int16_t)((long)d[idx] * (CxAF_NS - i) / CxAF_NS);
            }
    }
    if (!out->write(m_blkBuf, n))
        return false;
    return !stopFlag;
}

bool CXFFmpegAudioThread::writeScaled(const uint8_t* src, int n)
{
    if (volumePermil == 1000 && !muted) {
        if (!out->write(src, n))
            return false;
        return !stopFlag;
    }
    ensureBuf(n);
    const int16_t* s = (const int16_t*)src;
    int16_t* d = (int16_t*)m_blkBuf;
    int ns = n / 2;
    int scale = muted ? 0 : volumePermil;
    for (int i = 0; i < ns; i++) {
        long v = (long)s[i] * scale / 1000;
        if (v > 32767)
            v = 32767;
        if (v < -32768)
            v = -32768;
        d[i] = (int16_t)v;
    }
    if (!out->write(m_blkBuf, n))
        return false;
    return !stopFlag;
}

void CXFFmpegAudioThread::ensureBuf(int bytes)
{
    if (m_blkCap >= bytes)
        return;
    if (m_blkBuf != 0)
        delete[] m_blkBuf;
    m_blkBuf = new uint8_t[bytes];
    m_blkCap = bytes;
}

void CXFFmpegAudioThread::tailFlush()
{
    if (stopFlag || out == 0)
        return;
    int aCh = out->outCh();
    int aRate = out->outRate();
    if (aCh <= 0 || aRate <= 0)
        return;
    if (!dec->convertFlush() || dec->pcmBytes() <= 0)
        return;
    if (!writeScaled(dec->pcmData(), dec->pcmBytes()))
        return;
    long bpf2 = aCh * 2;
    long endMs = (m_lastMs >= 0 ? m_lastMs : 0) +
        (long)((double)(dec->pcmBytes() / (int)bpf2) * 1000.0 / aRate);
    m_clock->updateValue(endMs);
    post(CxFFmpegEvAudioPos, endMs);
}

void CXFFmpegAudioThread::post(int ev, long ms)
{
    QApplication::postEvent(sink, new CxFFmpegUiEvent(ev, ms, 0, 0));
}
