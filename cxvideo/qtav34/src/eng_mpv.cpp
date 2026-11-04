#include "eng_mpv.h"

// M0 存根:未链 libmpv。M1 起在 isAvailable() 中以 dlopen 探测 libmpv.so
// (不硬链接,保持"缺失自动降级"承诺),并实现 wid/render_context 渲染。
bool CXEngineMpv::isAvailable() const
{
    return false;
}

bool CXEngineMpv::play() { return false; }
bool CXEngineMpv::pause() { return false; }
bool CXEngineMpv::stop() { return false; }
bool CXEngineMpv::seek(long) { return false; }
bool CXEngineMpv::setRate(double) { return false; }
bool CXEngineMpv::setVolume(int) { return false; }
bool CXEngineMpv::setMute(bool) { return false; }
long CXEngineMpv::position() const { return 0; }
long CXEngineMpv::duration() const { return 0; }
