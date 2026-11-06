#ifndef CXFF_WRAP_H
#define CXFF_WRAP_H

// libav* 的 extern "C" 汇入(纯 C ABI,FFmpeg 7.1 self-guard 但显式包一层更稳)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#endif // CXFF_WRAP_H
