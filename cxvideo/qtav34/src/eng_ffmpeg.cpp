#include "eng_ffmpeg.h"

// M0 存根:M2 起落三线程管线(需过滤 FFmpeg dev/libmpv dev 的 pkg-config 探测,
// 在 cxvideo.pri 中定义 WITH_CX_ENGINE_*)
bool CXEngineFfmpeg::isAvailable() const
{
    return false;
}

bool CXEngineFfmpeg::play() { return false; }
bool CXEngineFfmpeg::pause() { return false; }
bool CXEngineFfmpeg::stop() { return false; }
bool CXEngineFfmpeg::seek(long) { return false; }
bool CXEngineFfmpeg::setRate(double) { return false; }
bool CXEngineFfmpeg::setVolume(int) { return false; }
bool CXEngineFfmpeg::setMute(bool) { return false; }
long CXEngineFfmpeg::position() const { return 0; }
long CXEngineFfmpeg::duration() const { return 0; }
