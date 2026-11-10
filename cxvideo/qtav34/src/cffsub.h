#ifndef CXFFSUB_H
#define CXFFSUB_H

#include "compat34.h"
#include "cxff_wrap.h"
#include <qstring.h>
#include <qmutex.h>
#include <qfile.h>
#include <qstringlist.h>

// 字幕基元(纯文本;时间 ms)——M3-A,detail.md §7.4
// 外挂 SRT 解析 + 内嵌文字字幕解码(mov_text/subrip/ass/ssa/webvtt;
// 不收 bitmap 型;p qgs/dvb/dvdsub 在 cffSubCodecTextual() 判否)

struct CxFFSubCue {
    long startMs;
    long endMs;
    QString text;
    CxFFSubCue() : startMs(0), endMs(0) { }
};

// 时间线(手滚数组,双 Qt 零容器坑;QMutex 保护)
class CxFFSubTimeline {
public:
    CxFFSubTimeline() : m_arr(0), m_n(0), m_cap(0) { }
    ~CxFFSubTimeline()
    {
        delete[] m_arr;
    }
    void clear()
    {
        QMutexLocker lk(&mx);
        m_n = 0;
    }
    void append(const CxFFSubCue& c)
    {
        QMutexLocker lk(&mx);
        if (m_n == m_cap) {
            int ncap = (m_cap > 0) ? m_cap * 2 : 64;
            CxFFSubCue* na = new CxFFSubCue[ncap];
            for (int i = 0; i < m_n; i++)
                na[i] = m_arr[i];
            delete[] m_arr;
            m_arr = na;
            m_cap = ncap;
        }
        m_arr[m_n++] = c;
    }
    int count()
    {
        QMutexLocker lk(&mx);
        return m_n;
    }
    bool lookup(long ms, QString& text)
    {
        QMutexLocker lk(&mx);
        for (int i = 0; i < m_n; i++) {
            if (ms >= m_arr[i].startMs && ms < m_arr[i].endMs) {
                text = m_arr[i].text;
                return true;
            }
        }
        return false;
    }

private:
    CxFFSubTimeline(const CxFFSubTimeline&);            // 禁拷
    CxFFSubTimeline& operator=(const CxFFSubTimeline&);
    QMutex mx;
    CxFFSubCue* m_arr;
    int m_n;
    int m_cap;
};

// 解析 "HH:MM:SS,mmm --> HH:MM:SS,mmm" 行;命中返回 true
inline bool cffSubParseSpan(const QString& ln, long& a, long& b)
{
    QByteArray bl = qToUtf8(ln);
    long h1, m1, s1, x1, h2, m2, s2, x2;
    char arrow[6];
    int n = sscanf(bl.data(), "%ld:%ld:%ld,%ld --> %ld:%ld:%ld,%ld",
                   &h1, &m1, &s1, &x1, &h2, &m2, &s2, &x2);
    (void)arrow;
    if (n != 8)
        return false;
    a = ((h1 * 60 + m1) * 60 + s1) * 1000 + x1;
    b = ((h2 * 60 + m2) * 60 + s2) * 1000 + x2;
    return true;
}

// 内标签剥除:ASS {\...} 花括号 + HTML <tag>;ASS \N/\n 行断转 '\n'
inline QString cffSubStripTags(const QString& t)
{
    QString out;
    bool inBrace = false;
    bool inTag = false;
    const QChar* q = t.unicode();
    int n = t.length();
    for (int i = 0; i < n; i++) {
        QChar ch = q[i];
        if (inBrace) {
            if (ch == '}')
                inBrace = false;
            continue;
        }
        if (inTag) {
            if (ch == '>')
                inTag = false;
            continue;
        }
        if (ch == '{') {
            inBrace = true;
            continue;
        }
        if (ch == '<') {
            inTag = true;
            continue;
        }
        if (ch == '\\') {
            // ASS 软断行 \N 或 \n(标签形式,非独立反斜杠转义)
            if (i + 1 < n && (q[i+1] == 'N' || q[i+1] == 'n')) {
                out += QChar('\n');
                i++;
                continue;
            }
        }
        out += ch;
    }
    return out;
}

// 外挂 SRT 解析(UTF-8;BOM 跳过);条目追加到 tl;文件在/解析无可断言即成功
inline bool cffParseSrt(const QString& path, CxFFSubTimeline* tl)
{
    QFile f(path);
    if (!qOpenReadOnly(f))
        return false;
    QByteArray all = f.readAll();
    f.close();
    const char* pd = all.data();
    int psz = all.size();
    if (psz >= 3 && (unsigned char)pd[0] == 0xEF &&
        (unsigned char)pd[1] == 0xBB && (unsigned char)pd[2] == 0xBF) {
        pd += 3;
        psz -= 3;
    }
    QString content = qFromUtf8(pd, psz);
    // CRLF 归一
    content.replace("\r\n", "\n");
    QStringList lines = qStrSplit(content, "\n", false);
    long s0 = 0, e0 = 0;
    for (int i = 0; i < (int)lines.count(); i++) {
        QString ln = qTrim(lines[i]);
        long a, b;
        if (!cffSubParseSpan(ln, a, b))
            continue;
        s0 = a;
        e0 = b;
        QString body;
        i++;
        for (; i < (int)lines.count(); i++) {
            QString tl_ln = qTrim(lines[i]);
            if (tl_ln.isEmpty())
                break; // 空行 = 条目结束
            if (!body.isEmpty())
                body += QChar('\n');
            body += cffSubStripTags(tl_ln);
        }
        if (!body.isEmpty() && e0 > s0) {
            CxFFSubCue c;
            c.startMs = s0;
            c.endMs = e0;
            c.text = body;
            tl->append(c);
        }
    }
    return true;
}

// 内嵌字幕 codec 是否文本型(pgs/dvb/dvdsub 等位图型一律否)
bool cffSubCodecTextual(int codecId);

// 内嵌文字字幕解码器(avcodec;deprecated decode_subtitle2 于 7.1 仍可用,
// lavc 60.1 起仅标记 deprecated;详见 detail.md §7.4 网搜记录)
class CxFFSubDecoder {
public:
    CxFFSubDecoder() : ctx(0) { m_tb.num = 1; m_tb.den = 1000; }
    ~CxFFSubDecoder() { close(); }

    bool init(AVStream* st, QString* err);
    void close();
    void flush();
    AVRational timeBase() const { return m_tb; }

    // 一包输出 0..n 条 cue(加入 tl);<0 = 解码错误(线程按致命结束)
    int decode(AVPacket* pkt, long pktPtsMs, CxFFSubTimeline* tl);

private:
    AVCodecContext* ctx;
    AVRational m_tb;
};

#endif // CXFFSUB_H
