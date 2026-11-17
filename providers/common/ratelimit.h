#pragma once
#include "compat.h"

#include <qstring.h>

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

// 全局限流(按 host 最小间隔):防止对同一站点突发请求(风控/被封的主要诱因)。
// 配置:QXVOTUBE_QPS(默认 12,<=0 关闭)。
// 两种用法:
//   acquire(host)      —— 阻塞到槽位(测试/工作线程用)
//   reserveSlot(host)  —— 非阻塞;返回还需等待的毫秒数并预约槽位
//                         (curl 引擎路径用:交给 HttpRequest.delayMs,不占线程)
// 注:Qt3 无 QHash,统一 std::map<QString,...>(QString 有 operator<)。
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
        double q = 0;
        {
            std::lock_guard<std::mutex> lk(state().mu);
            q = state().qps;
        }
        if (q <= 0) return;
        const long long intervalMs = static_cast<long long>(1000.0 / q);
        if (intervalMs <= 0) return;

        std::unique_lock<std::mutex> lk(state().mu);
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        std::map<QString, std::chrono::steady_clock::time_point>::iterator it =
            state().last.find(host);
        if (it != state().last.end()) {
            const long long elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second).count();
            if (elapsed < intervalMs) {
                const long long wait = intervalMs - elapsed;
                lk.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                lk.lock();
            }
        }
        state().last[host] = std::chrono::steady_clock::now();
    }

    // 非阻塞预约:返回需要等待的毫秒数(0=可立即发);同时把该槽位记为己占
    static long long reserveSlot(const QString& host) {
        double q = 0;
        {
            std::lock_guard<std::mutex> lk(state().mu);
            q = state().qps;
        }
        if (q <= 0) return 0;
        const long long intervalMs = static_cast<long long>(1000.0 / q);
        if (intervalMs <= 0) return 0;

        std::lock_guard<std::mutex> lk(state().mu);
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        long long wait = 0;
        std::map<QString, std::chrono::steady_clock::time_point>::iterator it =
            state().last.find(host);
        if (it != state().last.end()) {
            const long long elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second).count();
            if (elapsed < intervalMs) wait = intervalMs - elapsed;
        }
        state().last[host] =
            now + std::chrono::milliseconds(wait);   // 预约未来槽位
        return wait;
    }

private:
    struct State {
        std::mutex mu;
        std::map<QString, std::chrono::steady_clock::time_point> last;
        double qps = 12.0;
    };
    static State& state() {
        static State s;
        return s;
    }
};

} // namespace qxv
