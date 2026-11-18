#ifndef ABOGUS_H
#define ABOGUS_H
#include <string>
#include <cstdint>
namespace qxv { namespace abogus {
typedef double (*RandFn)(void* ctx);
// windowFp:窗口指纹字符串(格式见 abogus.cpp kWindowFp 注释);传 0 用内置默认。
std::string generate(const std::string& uri, const std::string& ua,
                     long long nowMs, void* randCtx, RandFn rand,
                     const char* windowFp = 0);
}}
#endif
