#include "cffsub.h"

// deprecated 解码闭包(FFmpeg 7.1 仅标注,未移除;Qt6 侧 -Wextra 会叫,压制)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

bool cffSubCodecTextual(int codecId)
{
    switch (codecId) {
    case AV_CODEC_ID_MOV_TEXT:
    case AV_CODEC_ID_TEXT:
    case AV_CODEC_ID_SUBRIP:
    case AV_CODEC_ID_ASS:
    case AV_CODEC_ID_SSA:
    case AV_CODEC_ID_WEBVTT:
        return true;
    default:
        return false; // 位图型(pgs/dvb/dvdsub)与其它一律否
    }
}

// ASS Dialogue 前缀字段判 meta:空 / 纯数字·时戳(0-9 . : - 空白) / Default 样式名
static bool cffAssFieldMeta(const char* b, const char* e)
{
    if (b == e)
        return true; // 空字段
    int n = (int)(e - b);
    if (n == 7 && (strncasecmp(b, "default", 7) == 0 ||
                   strncasecmp(b, "Lang1", 5) == 0))
        return true;
    for (const char* p = b; p < e; p++) {
        char ch = *p;
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ':' ||
            ch == '-' || ch == ' ' || ch == '\t')
            continue;
        return false; // 其它字母任意 = 正文起
    }
    return true;
}

bool CxFFSubDecoder::init(AVStream* st, QString* err)
{
    const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
    if (c == 0) {
        if (err != 0)
            *err = qFromUtf8("no subtitle decoder");
        return false;
    }
    ctx = avcodec_alloc_context3(c);
    if (ctx == 0)
        return false;
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
        avcodec_open2(ctx, c, 0) < 0) {
        avcodec_free_context(&ctx);
        ctx = 0;
        return false;
    }
    m_tb = st->time_base;
    return true;
}

void CxFFSubDecoder::close()
{
    if (ctx != 0) {
        avcodec_free_context(&ctx);
        ctx = 0;
    }
}

void CxFFSubDecoder::flush()
{
    if (ctx != 0)
        avcodec_flush_buffers(ctx);
}

int CxFFSubDecoder::decode(AVPacket* pkt, long pktPtsMs, CxFFSubTimeline* tl)
{
    AVSubtitle sub;
    memset(&sub, 0, sizeof(sub));
    int got = 0;
    int r = avcodec_decode_subtitle2(ctx, &sub, &got, pkt);
    if (r < 0) {
        avsubtitle_free(&sub);
        return -1;
    }
    if (!got) {
        avsubtitle_free(&sub);
        return 0;
    }
    // 内嵌 cue 时刻 = pkt pts + display_time;end 缺省(0/==start/0xFFFFFFFF)
    // 以 pkt duration 兜底(mov_text 即此形),再兜 3s 尾
    long startMs = pktPtsMs + (long)sub.start_display_time;
    long endMs = (sub.end_display_time == 0xFFFFFFFF) ?
                 (pktPtsMs + 5000) : (pktPtsMs + (long)sub.end_display_time);
    if (endMs == 0 || endMs <= startMs) {
        long durMs = -1;
        if (pkt != 0 && pkt->duration > 0 && m_tb.num > 0 && m_tb.den > 0) {
            AVRational msQ = { 1, 1000 };
            long long d = av_rescale_q(pkt->duration, m_tb, msQ);
            if (d > 0)
                durMs = (long)d;
        }
        if (durMs <= 0)
            durMs = 3000;
        endMs = startMs + durMs;
    }
    int n = 0;
    for (unsigned i = 0; i < sub.num_rects; i++) {
        AVSubtitleRect* rc = sub.rects[i];
        if (rc == 0)
            continue;
        const char* txt = 0;
        bool isAss = false;
        if (rc->type == SUBTITLE_TEXT)
            txt = rc->text;
        else if (rc->type == SUBTITLE_ASS) {
            txt = rc->ass;
            isAss = true;
        }
        if (txt == 0 || txt[0] == '\0')
            continue;
        // ASS Dialogue 行(mov_text/ass 均此形):开头若干字段为
        // Layer/Start/End/Style/Margins/Effect 等;ffmpeg mov_text 译出
        // "0,0,Default,,.." 系 8 字段、标准 ASS 为 9 字段。按字段内容跳:
        // 开头"纯数字/时戳/空/Default"字段废弃,首个正文段起取。
        if (isAss) {
            const char* p = txt;
            const char* stop = txt; // 首个非 meta 字段起点;整串全 meta 则不动
            while (*p != '\0') {
                const char* fend = strchr(p, ',');
                const char* fldEnd = (fend != 0) ? fend : p + strlen(p);
                if (!cffAssFieldMeta(p, fldEnd)) {
                    stop = p;
                    break;
                }
                if (fend == 0)
                    break;
                p = fend + 1;
                stop = p; // 后面可能直接是正文
            }
            txt = stop;
        }
        CxFFSubCue c;
        c.startMs = startMs;
        c.endMs = endMs;
        c.text = cffSubStripTags(qFromUtf8(txt));
        if (!c.text.isEmpty()) {
            tl->append(c);
            n++;
        }
    }
    avsubtitle_free(&sub);
    return n;
}
