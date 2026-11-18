#include "qjsrun.h"

#include <qprocess.h>
#include <string.h>
#include <vector>

#ifdef QT3_BUILD
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <qdatetime.h>
#endif

namespace qxv { namespace douyin {

#ifdef QT3_BUILD
// fork 后子进程只做 unsetenv/dup2/execvp(async-signal-safe),无 Qt 依赖
static bool runForked(const QStringList& argsIn, std::string* out, int timeoutMs)
{
    std::vector<std::string> keep;
    for (int i = 0; i < argsIn.size(); ++i)
        keep.push_back(std::string(qToUtf8(argsIn[i]).data()));
    std::vector<char*> argv;
    for (size_t i = 0; i < keep.size(); ++i)
        argv.push_back(const_cast<char*>(keep[i].c_str()));
    argv.push_back(0);

    int fds[2];
    if (pipe(fds) != 0) return false;
    const pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return false; }
    if (pid == 0) {
        unsetenv("LD_LIBRARY_PATH");
        unsetenv("LD_PRELOAD");
        dup2(fds[1], 1);
        const int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 2); close(dn); }
        close(fds[0]); close(fds[1]);
        execvp(argv[0], &argv[0]);
        _exit(127);
    }
    close(fds[1]);
    out->clear();
    QTime t;
    t.start();
    char buf[4096];
    for (;;) {
        const int remain = timeoutMs - t.elapsed();
        if (remain <= 0) { kill(pid, SIGKILL); break; }
        struct pollfd pfd;
        pfd.fd = fds[0]; pfd.events = POLLIN; pfd.revents = 0;
        const int pr = poll(&pfd, 1, remain > 200 ? 200 : remain);
        if (pr > 0) {
            const ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0) out->append(buf, (size_t)n);
            else break;                       // EOF
        }
    }
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 && !out->empty();
}
#endif

bool runQjs(const QStringList& args, std::string* out, int timeoutMs)
{
    if (args.isEmpty() || out == 0) return false;
#ifdef QT3_BUILD
    return runForked(args, out, timeoutMs);
#else
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    // 剔除 LD_LIBRARY_PATH/LD_PRELOAD,防 vcpkg 私有库遮蔽 qjs 依赖
    QStringList env;
    const QStringList sysEnv = QProcess::systemEnvironment();
    for (int i = 0; i < sysEnv.size(); ++i) {
        if (sysEnv[i].startsWith(qFromUtf8("LD_LIBRARY_PATH="))
            || sysEnv[i].startsWith(qFromUtf8("LD_PRELOAD=")))
            continue;
        env.append(sysEnv[i]);
    }
    p.setEnvironment(env);
    p.start(args.first(), args.mid(1));
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return false;
    const QByteArray b = p.readAllStandardOutput();
    out->assign(b.constData(), (size_t)b.size());
    return !out->empty();
#endif
}

}} // namespace qxv::douyin
