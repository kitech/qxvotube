#pragma once
#include "qlcompat34.h"
#include <functional>
#include <qobject.h>
#include <qstring.h>

class QProcess;

// 媒体下载器(ffmpeg 落盘 + 真实进度;qlvotube 2026-11-18)
//   ffmpeg -nostdin -y -i <url> -c copy -progress pipe:1 -nostats <out>.mp4
// 进度:0-100 百分比;时长未知 → -1;失败 → -2;完成 → 100
// 并发 1(重复 start 返回 false);下载在独立进程,不影响播放。
class Downloader : public QObject {
    Q_OBJECT
public:
    typedef std::function<void(int pct)> ProgressFn;
    typedef std::function<void(bool ok, const QString& path,
                               const QString& err)> DoneFn;

    explicit Downloader(QObject* parent = 0);
    ~Downloader();

    bool busy() const { return m_proc != 0; }
    // url=媒体直链;durationMs=已知时长(0=未知);outDir 默认 ~/Downloads/qlvotube
    /* referer/ua/proxy:媒体防盗链必需(bilibili 需 Referer+UA;YouTube 需代理) */
    bool start(const QString& url, const QString& title, long durationMs,
               ProgressFn onPct, DoneFn onDone,
               const QString& outDir = QString(),
               const QString& referer = QString(),
               const QString& ua = QString(),
               const QString& proxy = QString());
    void cancel();

private slots:
    void onProgressData();
    void onExited();

private:
    void emitPct(int pct);
    void finish(bool ok, const QString& err);

    QProcess* m_proc;
    long m_durMs;
    int m_lastPct;
    QString m_out;
    QString m_errTail;
    ProgressFn m_onPct;
    DoneFn m_onDone;
};
