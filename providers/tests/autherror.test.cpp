// 认证错误模型离线验收(无网络):
//   ① credContext 推断(cookie/oauth/password/none)
//   ② authFail 写入 detail 键(auth.*/http.status/server.*)
//   ③ i18nKeyFor(Result) 选键正确
//   ④ 脱敏:凭证明文不得出现在 message/detail
//   ⑤ sanitizeServerMsg 截断/去换行
#include "common/autherror.h"
#include "common/errortext.h"

#include <QCoreApplication>
#include <cstdio>

using namespace qxv;

static int g_fail = 0;
static void check(bool ok, const char* what, const QString& got = QString()) {
    if (ok) std::printf("[ ok ] %s\n", what);
    else { ++g_fail; std::printf("[FAIL] %s %s\n", what, qToUtf8(got).data()); }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    // ① credContext
    {
        AuthContext n = credContext(QVariantMap());
        check(n.mode == AuthMode::None && !n.provided, "guest -> None");

        QVariantMap c;
        c.insert(QString::fromUtf8("cookie"),
                 QString::fromUtf8("a1=x; web_session=SECRET"));
        AuthContext cc = credContext(c);
        check(cc.mode == AuthMode::Cookie && cc.provided, "cookie -> Cookie");

        QVariantMap t;
        t.insert(QString::fromUtf8("token"), QString::fromUtf8("TOKEN"));
        check(credContext(t).mode == AuthMode::OAuth, "token -> OAuth");

        QVariantMap p;
        p.insert(QString::fromUtf8("username"), QString::fromUtf8("u"));
        check(credContext(p).mode == AuthMode::Password, "username -> Password");
    }

    // ② detail 键 + ③ 选键 + ④ 脱敏
    {
        AuthContext guest;
        Result r1 = authFail(Err::NeedAuth, guest,
                             QString::fromUtf8("xhs 搜索需登录"), 200,
                             QString::fromUtf8("-101"),
                             QString::fromUtf8("无登录信息"));
        check(r1.detail.value(QString::fromUtf8("auth.mode")).toString()
                  == QString::fromUtf8("none"), "detail auth.mode=none");
        check(r1.detail.value(QString::fromUtf8("auth.provided")).toBool() == false,
              "detail auth.provided=false");
        check(r1.detail.value(QString::fromUtf8("http.status")).toInt() == 200,
              "detail http.status");
        check(r1.detail.value(QString::fromUtf8("server.code")).toString()
                  == QString::fromUtf8("-101"), "detail server.code");
        check(i18nKeyFor(r1) == QString::fromUtf8("error.auth_guest_need_login"),
              "key: guest");

        AuthContext ck;
        ck.mode = AuthMode::Cookie;
        ck.provided = true;
        ck.expired = true;
        Result r2 = authFail(Err::AuthExpired, ck, QString::fromUtf8("cookie 失效"));
        check(i18nKeyFor(r2) == QString::fromUtf8("error.auth_cookie_expired"),
              "key: cookie expired");
        check(!r2.message.contains(QString::fromUtf8("SECRET")),
              "message 不含凭证明文");

        AuthContext oa;
        oa.mode = AuthMode::OAuth;
        oa.provided = true;
        oa.expired = true;
        check(i18nKeyFor(authFail(Err::AuthExpired, oa, QString::fromUtf8("t")))
                  == QString::fromUtf8("error.auth_oauth_expired"),
              "key: oauth expired");

        AuthContext pw;
        pw.mode = AuthMode::Password;
        pw.provided = true;
        check(i18nKeyFor(authFail(Err::NeedAuth, pw, QString::fromUtf8("p")))
                  == QString::fromUtf8("error.auth_password_failed"),
              "key: password failed");

        check(i18nKeyFor(Result::fail(Err::Network, QString::fromUtf8("n")))
                  == QString::fromUtf8("error.network"), "key: network 不变");
    }

    // ⑥ withServer:非认证错误只写 server.* 不写 auth.*
    {
        Result w = withServer(Result::fail(Err::RateLimit, QString::fromUtf8("x")),
                              200, QString::fromUtf8("-412"),
                              QString::fromUtf8("请求过于频繁"));
        check(w.detail.contains(QString::fromUtf8("http.status"))
                  && !w.detail.contains(QString::fromUtf8("auth.mode")),
              "withServer: 不写 auth.*");
        check(w.detail.value(QString::fromUtf8("server.code")).toString()
                  == QString::fromUtf8("-412"), "withServer: server.code");
        check(i18nKeyFor(w) == QString::fromUtf8("error.ratelimit"),
              "withServer: i18n 仍按 Err");
    }

    // ⑤ sanitize
    {
        QString longTail;
        longTail.fill(QChar(0x78), 200);   // 'x' * 200
        const QString s = sanitizeServerMsg(
            QString::fromUtf8("line1\r\nline2  ") + longTail);
        check(!s.contains(QChar(0x0A)) && s.size() <= 121,
              "sanitize: 去换行+截断", QString::number(s.size()));
    }

    std::printf("autherror: %s\n", g_fail ? "FAILED" : "all green");
    return g_fail ? 1 : 0;
}
