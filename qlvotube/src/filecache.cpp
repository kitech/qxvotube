#include "filecache.h"
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qdatetime.h>
#include <qmap.h>
#ifdef QT3_BUILD
#include <qapplication.h>
#else
#include <QDebug>
#endif
#include <cstdlib>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace {

QString& baseDirRef() { static QString d; return d; }

QString pathJoin(const QString& dir, const QString& name)
{
    return dir + qFromUtf8("/") + name;
}

/* Qt3: QFileInfoList = QPtrList<QFileInfo>;Qt4+: QList<QFileInfo> */
int fcCount(const QFileInfoList* l) { return l ? l->count() : 0; }
#ifdef QT3_BUILD
const QFileInfo& fcAt(const QFileInfoList* l, int i)
{
    return *const_cast<QFileInfoList*>(l)->at(i);   /* Qt3 QPtrList::at 非 const */
}
#else
const QFileInfo& fcAt(const QFileInfoList* l, int i) { return l->at(i); }
#endif

void fcLog(const QString& s)
{
    if (!getenv("QLVOTUBE_DEBUG")) return;
#if QT_VERSION >= 0x060000
    qDebug().noquote() << s;
#else
    qDebug("%s", qToUtf8(s).data());
#endif
}

void trimDirNowImpl(const QString& dirPath, long long maxBytes)
{
    QDir dir(dirPath);
    if (!dir.exists()) return;
#ifdef QT3_BUILD
    const QFileInfoList* infos = dir.entryInfoList(QDir::Files, QDir::Time);
#else
    const QFileInfoList values = dir.entryInfoList(QDir::Files, QDir::Time);
    const QFileInfoList* infos = &values;
#endif
    const int n = fcCount(infos);
    const QDateTime now = QDateTime::currentDateTime();

    /* 清 >1h 的 .part 残渣 */
    for (int i = 0; i < n; ++i) {
        const QFileInfo& fi = fcAt(infos, i);
        if (!fi.fileName().endsWith(qFromUtf8(".part"))) continue;
        if (fi.lastModified().secsTo(now) >= 3600)
            QFile::remove(fi.filePath());
    }

    long long total = 0;
    for (int i = 0; i < n; ++i) {
        const QFileInfo& fi = fcAt(infos, i);
        if (fi.fileName().endsWith(qFromUtf8(".part"))) continue;
        total += (long long)fi.size();
    }
    if (total <= maxBytes) return;

    int removed = 0;
    /* QDir::Time:最新在前 → 从尾部(最旧)删;跳过 1h 内新文件(在途保护) */
    for (int i = n - 1; i >= 0 && total > maxBytes; --i) {
        const QFileInfo& fi = fcAt(infos, i);
        if (fi.fileName().endsWith(qFromUtf8(".part"))) continue;
        if (fi.lastModified().secsTo(now) < 3600) continue;
        const long long size = (long long)fi.size();
        if (QFile::remove(fi.filePath())) { total -= size; ++removed; }
    }
    fcLog(QString(qFromUtf8("[cache] trim dir=%1 removed=%2 remainBytes=%3"))
              .arg(dirPath).arg(removed).arg((double)total));
}

struct TrimWorker {
    std::thread th;
    std::mutex mu;
    std::condition_variable cv;
    std::map<QString, long long> pending;
    bool stop;

    TrimWorker() : stop(false) { th = std::thread(&TrimWorker::run, this); }
    ~TrimWorker() { shutdown(); }

    void post(const QString& dir, long long maxBytes) {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (stop) return;
            pending[dir] = maxBytes;   /* 同目录合并:后者覆盖 */
        }
        cv.notify_one();
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu);
            stop = true;
        }
        cv.notify_all();
        if (th.joinable()) th.join();
    }
    void run() {
        for (;;) {
            std::map<QString, long long> jobs;
            {
                std::unique_lock<std::mutex> lk(mu);
                cv.wait(lk, [this] { return stop || !pending.empty(); });
                if (pending.empty()) {
                    if (stop) return;
                    continue;
                }
                jobs.swap(pending);
            }
            for (std::map<QString, long long>::iterator it = jobs.begin();
                 it != jobs.end(); ++it)
                trimDirNowImpl(it->first, it->second);
        }
    }
};

TrimWorker& worker() { static TrimWorker w; return w; }

} // namespace

namespace filecache {

void init(const QString& baseDir)
{
    baseDirRef() = baseDir;
    if (!QDir(baseDir).exists()) QDir().mkdir(baseDir);     /* 父 ~/.cache 已存在 */
    const QString cov = coversDir();
    if (!QDir(cov).exists()) QDir().mkdir(cov);
}

QString coversDir() { return pathJoin(baseDirRef(), qFromUtf8("covers")); }

QString keyForUrl(const QString& url)
{
    QString u = url;
    int q = qIndexOf(u, qFromUtf8("?"));
    if (q >= 0) u = u.left(q);
    int h = qIndexOf(u, qFromUtf8("#"));
    if (h >= 0) u = u.left(h);

    QString ext;
    int slash = qLastIndexOf(u, qFromUtf8("/"));
    QString last = slash >= 0 ? u.mid(slash + 1) : u;
    int dot = qLastIndexOf(last, qFromUtf8("."));
    if (dot > 0 && last.length() - dot <= 6)
        ext = qToLower(last.mid(dot));

    const QByteArray ba = qToUtf8(u);
    unsigned long long hsh = 1469598103934665603ULL;
    for (int i = 0; i < ba.size(); ++i) {
        hsh ^= (unsigned char)ba[i];
        hsh *= 1099511628211ULL;
    }
    QString name = QString::number((uint)(hsh >> 32), 16)
                 + QString::number((uint)hsh, 16);
    return name + ext;
}

QString writeFile(const QString& dir, const QString& key, const QByteArray& data)
{
    if (dir.isEmpty() || key.isEmpty() || data.isEmpty()) return QString();
    QDir d(dir);
    if (!d.exists()) QDir().mkdir(dir);
    const QString path = pathJoin(dir, key);
    if (QFile::exists(path)) return path;      /* 已有不覆盖 */
    const QString part = path + qFromUtf8(".part");
    QFile f(part);
#ifdef QT3_BUILD
    if (!f.open(IO_WriteOnly)) return QString();
#else
    if (!f.open(QIODevice::WriteOnly)) return QString();
#endif
#ifdef QT3_BUILD
    const bool ok = (f.writeBlock(data) == (Q_LONG)data.size());
#else
    const bool ok = (f.write(data) == (qint64)data.size());
#endif
    f.close();
    if (!ok) { QFile::remove(part); return QString(); }
    if (QFile::exists(path)) { QFile::remove(part); return path; }
    if (!QDir().rename(part, path)) { QFile::remove(part); return QString(); }
    return path;
}

QByteArray readFile(const QString& dir, const QString& key)
{
    QFile f(pathJoin(dir, key));
    if (!f.exists()) return QByteArray();
#ifdef QT3_BUILD
    if (!f.open(IO_ReadOnly)) return QByteArray();
#else
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
#endif
    QByteArray out = f.readAll();
    f.close();
    return out;
}

bool removeFile(const QString& dir, const QString& key)
{
    return QFile::remove(pathJoin(dir, key));
}

void requestTrim(const QString& dir, long long maxBytes)
{
    worker().post(dir, maxBytes);
}

void shutdown()
{
    worker().shutdown();
}

void trimDirNow(const QString& dir, long long maxBytes)
{
    trimDirNowImpl(dir, maxBytes);
}

long long totalBytes(const QString& dir)
{
    QDir d(dir);
    if (!d.exists()) return 0;
#ifdef QT3_BUILD
    const QFileInfoList* infos = d.entryInfoList(QDir::Files, QDir::Time);
#else
    const QFileInfoList values = d.entryInfoList(QDir::Files, QDir::Time);
    const QFileInfoList* infos = &values;
#endif
    long long total = 0;
    for (int i = 0; i < fcCount(infos); ++i) {
        const QFileInfo& fi = fcAt(infos, i);
        if (fi.fileName().endsWith(qFromUtf8(".part"))) continue;
        total += (long long)fi.size();
    }
    return total;
}

int fileCount(const QString& dir)
{
    QDir d(dir);
    return d.exists() ? d.entryList(QDir::Files).count() : 0;
}

} // namespace filecache
