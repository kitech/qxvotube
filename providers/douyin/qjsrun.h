#pragma once
#include "common/compat.h"
#include <string>

// qjs(quickjs-ng)命令行执行器 —— 双机制对照共用
//   Qt3:fork/execvp + 管道 poll(不走 QProcess:进程内有 eventpoller 泵线程时
//        Qt3 QProcess 会出现 "normalExit=1 但 readStdout 空");
//   Qt4+:QProcess::waitForFinished。
// 两者都剔除 LD_LIBRARY_PATH/LD_PRELOAD:父进程的 vcpkg/qt 私有库路径会
// 遮蔽 qjs 依赖的 libqjs,子进程启动即 "symbol lookup error" 退出。
// 返回 false = 启动失败/超时/非零退出/无输出(对照缺席,不视为签名失败)。
namespace qxv { namespace douyin {

bool runQjs(const QStringList& args, std::string* out, int timeoutMs);

}} // namespace qxv::douyin
