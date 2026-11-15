#pragma once
#include "compat.h"

#include <QHash>
#include <QString>

#include <chrono>
#include <mutex>
#include <thread>

// 全局限流(按 host 最小间隔):防止对同一站点突发请求(风控/被封的主要诱因)。
// 配置:QXVOTUBE_QPS(默认 12,<=0 关闭)。
// 调用点:NetClient::doOnce 在建立连接前 acquire();在工作线程里阻塞,不占 UI。
namespace qxv {

class RateLimiter {
public:
    static void configure(double qps) {
        std::lock_guard<std::mutex> lk(state().mu);
        state().qps = qps;
    }
    static double qps() {
        std::lock_guard<std::mutex> lk(state().mu);
        return state().qps;
    }

    // 阻塞到该 host 满足最小间隔(不同 host 互不影响)
    static void acquire(const QString& host) {
        State& s = state();
        double q = 0;
        {
            std::lock_guard<std::mutex> lk(s.mu);
            q = s.qps;
        }
        if (q <= 0) return;
        const long long intervalMs = static_cast<long long>(1000.0 / q);
        if (intervalMs <= 0) return;

        std::unique_lock<std::mutex> lk(s.mu);
        const auto now = std::chrono::steady_clock::now();
        auto it = s.last.find(host);
        if (it != s.last.end()) {
            const long long elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - it.value())
                    .count();
            if (elapsed < intervalMs) {
                const long long wait = intervalMs - elapsed;
                lk.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                lk.lock();
            }
        }
        s.last[host] = std::chrono::steady_clock::now();
    }

private:
    struct State {
        std::mutex mu;
        QHash<QString, std::chrono::steady_clock::time_point> last;
        double qps = 12.0;
    };
    static State& state() {
        static State s;
        return s;
    }
};

} // namespace qxv
