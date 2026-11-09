#ifndef CXFFAO_H
#define CXFFAO_H

#include "cxff_wrap.h"
#include <qstring.h>

// 音频输出统一接口(M2.1;交错 S16;单线程 = athread;write 阻塞)
// 契约:open 失败由引擎降级下一后端/silent
class CXAudioOut {
public:
    virtual ~CXAudioOut() { }

    virtual const char* backend() const = 0;
    virtual bool open(int ch, int rate) = 0;
    virtual bool write(const uint8_t* data, int bytes) = 0; // 阻塞至此块可播
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void clear() = 0;     // seek:丢缓冲立即静音
    virtual int latencyMs() = 0;  // 缓冲未播时长(时钟校准)
    virtual void drain() = 0;     // EOF:等排空
    virtual void close() = 0;

    int outCh() const { return m_obtCh; }
    int outRate() const { return m_obtRate; }

protected:
    int m_obtCh;
    int m_obtRate;
};

// 各后端构造(未编入的后端返回 0;pri 宏门控)
CXAudioOut* cffaoNewPulse();
CXAudioOut* cffaoNewAlsa();
CXAudioOut* cffaoNewSdl();
CXAudioOut* cffaoNewSilent();

// 工厂:auto = pulse → alsa → sdl → silent;指定名失败回退 silent
CXAudioOut* cffaoCreate(const char* want);

#endif // CXFFAO_H
