#ifndef FILECACHE_H
#define FILECACHE_H

#include "qlcompat34.h"

// 通用磁盘文件缓存 + 异步 mtime-LRU 清理(Qt3/Qt4 通用)。
// - writeFile 原子写(.part→rename);requestTrim 入队后台线程,GUI 零阻塞。
// - trimDirNow 只应被后台线程/测试调用,绝不在 GUI 线程直接调用。
namespace filecache {

void init(const QString& baseDir);        // ~/.cache/qlvotube
QString coversDir();                      // <base>/covers

QString keyForUrl(const QString& url);    // URL→稳定文件名(hash+扩展名)

QString writeFile(const QString& dir, const QString& key, const QByteArray& data);
QByteArray readFile(const QString& dir, const QString& key);
bool removeFile(const QString& dir, const QString& key);

void requestTrim(const QString& dir, long long maxBytes);  // 异步,合并同目录
void shutdown();                                           // 退出:停止并 join

void trimDirNow(const QString& dir, long long maxBytes);   // 仅后台线程
long long totalBytes(const QString& dir);
int fileCount(const QString& dir);

} // namespace filecache

#endif // FILECACHE_H
