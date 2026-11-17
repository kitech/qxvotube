#pragma once
#include <functional>
#ifdef QT3_BUILD
#include <qobject.h>
#else
#include <QObject>
#endif
#include "compatcore34.h"   // qlcomp:qFromUtf8/qbaFromCharptr 等(字符串全 UTF-8 口径)
#include "qt3shim.h"        // Qt3:QVariantMap 等缺失类型补齐(实现在 Qt>=5)
// providers 跨 Qt 版本兼容宏(目标:Qt3/4/6 同一份源码语义)
#ifdef QT3_BUILD
#include <qglobal.h>        // Qt3:QtGlobal 还没改名
#else
#include <qglobal.h>
#endif

// ── 域类型:请求/响应头 ─────────────────────────────────────
// QList36 / QGuard36 定义在 qlcomp/compatcore34.h(单一来源)。
#ifdef QT3_BUILD
#include <qpair.h>
#else
#include <qpair.h>
#endif
typedef QList36<QPair<QString, QString> > NetHeaders;

#if QT_VERSION >= 0x050000
#include <qurlquery.h>          // Qt>=5 原生;Qt3/Qt4 用 qlcomp 同名实现
#endif

// ── 字符串字面量 ──────────────────────────────────────────────
// 项目口径:字符串全用 utf8。统一走 qlcomp 的 qFromUtf8("..."),
// 禁 QLatin1String/QLatin1Char/QStringLiteral/QXV_STR(已废弃)。

// ── 大小写不敏感比较 / 环境变量(Qt3 兼容,统一函数入口)──────
// 不用宏:Qt3 无 QString::compare(Qt::CaseInsensitive),用 qToUpper 实现。
inline bool qieq(const QString& a, const QString& b) {
    return qToUpper(a) == qToUpper(b);
}
#include <cstdlib>
inline QString qenv(const char* var) {
    const char* v = std::getenv(var);
    return v ? QString::fromUtf8(v) : QString();
}
inline bool qenv_empty(const char* var) {
    const char* v = std::getenv(var);
    return v == 0 || v[0] == '\0';
}


// QByteArray 前 n 字节 → QString(Qt3 QByteArray 无 left())
inline QString qbaHead(const QByteArray& b, int n) {
    if (n > (int)b.size()) n = (int)b.size();
    if (n < 0) n = 0;
    return qFromUtf8(qbaData(b), n);
}

// ── 延时执行(替代 Qt5.4+ 的 QTimer::singleShot(ms, lambda))──────
// Qt3/Qt4 无函数对象版 singleShot:用 QObject::startTimer + timerEvent 实现。
// 所有权自管:触发后 deleteLater()。
class QxvShot : public QObject {
public:
    QxvShot(int ms, std::function<void()> fn)
        : m_fn(fn), m_timer(startTimer(ms < 0 ? 0 : ms)) {}
    ~QxvShot() { if (m_timer) killTimer(m_timer); }
protected:
    void timerEvent(QTimerEvent*) {
        if (m_timer) { killTimer(m_timer); m_timer = 0; }
        std::function<void()> fn = m_fn;
        m_fn = std::function<void()>();
        if (fn) fn();
        deleteLater();
    }
private:
    std::function<void()> m_fn;
    int m_timer;
};
inline void qSingleShot(int ms, std::function<void()> fn) {
    new QxvShot(ms, fn);
}

// QStringList 按值删除(Qt3: remove(T);Qt4+: removeAll(T))
template<class L>
inline void qstrListRemoveOne(L& l, const QString& s) {
#if QT_VERSION < 0x040000
    l.remove(s);
#else
    l.removeAll(s);
#endif
}
