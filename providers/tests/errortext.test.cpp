// errortext 契约锁定:每个 Err 枚举值都有 i18n key;三语 lang JSON 键完整
// 运行:./ettest [langdir](缺省 ../qlvotube/lang 相对 cwd;或环境 QXV_LANG_DIR)
#include "compat.h"
#include "common/errortext.h"
#include "common/result.h"
#include "common/qcjson.h"
#include "common/debuglog.h"
#include <QCoreApplication>
#include <QFile>
#include <cstdio>

using namespace qxv;

static bool readJson(const QString& path, JRoot* out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    out->parse(f.readAll());
    return out->ok();
}

static bool checkLang(const QString& dir, const QString& lang, int* missing) {
    JRoot root;
    if (!readJson(dir + qFromUtf8("/%1.json").arg(lang), &root)) {
        std::printf("[FAIL] %s.json unreadable\n", qToUtf8(lang).data());
        ++(*missing);
        return false;
    }
    cJSON* eobj = jobj(root.p, "error");
    if (!eobj) { std::printf("[FAIL] %s: no error object\n", qToUtf8(lang).data()); ++(*missing); return false; }
    for (int code = (int)Err::Network; code <= (int)Err::Unsupported; ++code) {
        const QString key = i18nKeyFor(static_cast<Err>(code));
        if (key.isEmpty()) continue;   // Ok/Cancelled 不需要文案
        const QString sub = key.mid(qIndexOf(key, qFromUtf8(".")) + 1);
        const QString val = jstr(eobj, sub.toUtf8().constData());
        if (val.isEmpty()) {
            std::printf("[FAIL] %s: missing %s\n", qToUtf8(lang).data(), qToUtf8(key).data());
            ++(*missing);
        }
    }
    // 认证维度键(与 Err 正交,需单独校验)
    const char* authKeys[] = {
        "auth_guest_need_login", "auth_cookie_missing", "auth_cookie_expired",
        "auth_oauth_expired", "auth_password_failed", "auth_server" };
    for (const char* k : authKeys)
        if (jstr(eobj, k).isEmpty()) {
            std::printf("[FAIL] %s: missing error.%s\n", qToUtf8(lang).data(), k);
            ++(*missing);
        }
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    dbg::loadFromEnv();
    const QString dir = argc > 1 ? QString::fromUtf8(argv[1])
                                 : qFromUtf8("qlvotube/lang");
    int missing = 0;
    checkLang(dir, qFromUtf8("zh-CN"), &missing);
    checkLang(dir, qFromUtf8("en-US"), &missing);
    checkLang(dir, qFromUtf8("zh-TW"), &missing);
    if (missing == 0) std::printf("[ ok ] errortext: all Err codes have i18n keys in 3 langs\n");
    return missing == 0 ? 0 : 1;
}
