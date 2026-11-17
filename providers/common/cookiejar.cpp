#include "compat.h"
#include "cookiejar.h"
#include <qurl.h>
#include <qstringlist.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

// 存储口径:每域一个文本文件(name=value 每行一条),落在
//   ~/.cache/qxvproviders/cookies/<domain>.txt
// 不用 QSettings:Qt3 的 QSettings API 与 Qt4+ 差异大(readEntry/writeEntry),
// 这里统一走 stdio,三档 Qt 同码。

namespace qxv {

static QString cookieDir() {
    return qGetHomePath() + qFromUtf8("/.cache/qxvproviders/cookies");
}

static QString fileForDomain(const QString& dom) {
    return cookieDir() + qFromUtf8("/") + dom + qFromUtf8(".txt");
}

static bool readMap(const QString& path,
                    std::map<std::string, std::string>* out) {
    FILE* f = std::fopen(qToUtf8(path).data(), "r");
    if (!f) return false;
    char line[4096];
    while (std::fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char* eq = strchr(line, '=');
        if (!eq || eq == line) continue;
        *eq = '\0';
        (*out)[std::string(line)] = std::string(eq + 1);
    }
    std::fclose(f);
    return true;
}

static void writeMap(const QString& path,
                     const std::map<std::string, std::string>& m) {
    qMkdir(cookieDir(), true);
    FILE* f = std::fopen(qToUtf8(path).data(), "w");
    if (!f) return;
    for (std::map<std::string, std::string>::const_iterator it = m.begin();
         it != m.end(); ++it)
        std::fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
    std::fclose(f);
}

// "a=1; Path=/; HttpOnly" → name/value(属性忽略)
static bool parseCookiePair(const QString& line, QString* name, QString* value) {
    const int semi = qIndexOf(line, qFromUtf8(";"));
    const QString kv = (semi >= 0) ? line.left(semi) : line;
    const int eq = qIndexOf(kv, qFromUtf8("="));
    if (eq <= 0) return false;
    *name = qTrim(kv.left(eq));
    *value = qTrim(kv.mid(eq + 1));
    return !name->isEmpty();
}

QString CookieJar::domainFor(const QString& url) const {
    return qToLower(QUrl(url).host());
}

QString CookieJar::cookieForUrl(const QString& url) const {
    const QString dom = domainFor(url);
    if (dom.isEmpty()) return QString();

    // 只取命中最具体的一层;无命中再看父域
    QString host = dom;
    for (int hop = 0; hop < 2 && !host.isEmpty(); ++hop) {
        std::map<std::string, std::string> m;
        if (readMap(fileForDomain(host), &m) && !m.empty()) {
            QString out;
            for (std::map<std::string, std::string>::const_iterator it = m.begin();
                 it != m.end(); ++it) {
                if (!out.isEmpty()) out += qFromUtf8("; ");
                out += qFromUtf8(it->first.c_str()) + qFromUtf8("=")
                     + qFromUtf8(it->second.c_str());
            }
            return out;
        }
        const int dot = qIndexOf(host, qFromUtf8("."));
        if (dot <= 0) break;
        host = host.mid(dot + 1);
    }
    return QString();
}

void CookieJar::storeFromResponse(const QString& url, const NetHeaders& headers) {
    const QString dom = domainFor(url);
    if (dom.isEmpty()) return;

    std::map<std::string, std::string> m;
    readMap(fileForDomain(dom), &m);
    bool dirty = false;
    for (int h = 0; h < headers.size(); ++h) {
        if (!qieq(headers[h].first, qFromUtf8("Set-Cookie"))) continue;
        // curl 引擎:同名多值用 '\n' 拼接
        const QStringList lines = qStrSplit(headers[h].second, qFromUtf8("\n"), true);
        for (int i = 0; i < lines.size(); ++i) {
            QString name, value;
            if (!parseCookiePair(lines[i], &name, &value)) continue;
            if (qieq(name, qFromUtf8("deleted")) || value.isEmpty()) {
                m.erase(std::string(qToUtf8(name).data()));
                dirty = true;
                continue;
            }
            m[std::string(qToUtf8(name).data())] = std::string(qToUtf8(value).data());
            dirty = true;
        }
    }
    if (dirty) writeMap(fileForDomain(dom), m);
}

void CookieJar::importCookieString(const QString& domain,
                                   const QString& cookieStr) {
    const QString dom = qToLower(domain);
    std::map<std::string, std::string> m;
    readMap(fileForDomain(dom), &m);
    const QStringList pairs = qStrSplit(cookieStr, qFromUtf8(";"), true);
    for (int i = 0; i < pairs.size(); ++i) {
        QString name, value;
        if (!parseCookiePair(pairs[i], &name, &value)) continue;
        m[std::string(qToUtf8(name).data())] = std::string(qToUtf8(value).data());
    }
    writeMap(fileForDomain(dom), m);
}

void CookieJar::clear() {
    // 逐域文件删除(域数很少)
    const QString dir = cookieDir();
    FILE* p = popen(qToUtf8(qFromUtf8("ls -1 ") + dir
                          + qFromUtf8(" 2>/dev/null")).data(), "r");
    if (!p) return;
    char line[1024];
    QStringList files;
    while (std::fgets(line, sizeof(line), p)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0]) files.append(qFromUtf8(line));
    }
    pclose(p);
    for (int i = 0; i < files.size(); ++i)
        std::remove((qToUtf8(dir + qFromUtf8("/") + files[i])).data());
}

} // namespace qxv
