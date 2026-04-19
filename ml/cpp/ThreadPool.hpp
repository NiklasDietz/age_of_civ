#pragma once

/**
 * @file ThreadPool.hpp
 * @brief Header-only fixed-size thread pool for GA training work.
 *
 * One pool is created in main() and reused across all generations and for any
 * future parallel work (init, sensitivity sweeps, etc). Avoids per-generation
 * thread churn and lets callers submit fine-grained tasks (e.g. individual
 * game-level fitness evaluations) without worrying about pool lifetime.
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace aoc::ga {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t workerCount) {
        if (workerCount == 0) { workerCount = 1; }
        this->workers_.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            this->workers_.emplace_back([this] { this->workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(this->mu_);
            this->stopping_ = true;
        }
        this->cv_.notify_all();
        for (std::thread& t : this->workers_) {
            if (t.joinable()) { t.join(); }
        }
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] std::size_t size() const noexcept {
        return this->workers_.size();
    }

    /// Submit a task returning std::future<R>. The future can be waited on
    /// individually, or via waitFutures(...) as a batch.
    template <class F, class... Args>
    [[nodiscard]] auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto taskPtr = std::make_shared<std::packaged_task<R()>>(
            [fn = std::forward<F>(f),
             argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                return std::apply(std::move(fn), std::move(argsTuple));
            });
        std::future<R> fut = taskPtr->get_future();
        {
            std::lock_guard<std::mutex> lock(this->mu_);
            if (this->stopping_) {
                throw std::runtime_error("ThreadPool: submit on stopping pool");
            }
            this->tasks_.emplace([taskPtr] { (*taskPtr)(); });
        }
        this->cv_.notify_one();
        return fut;
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(this->mu_);
                this->cv_.wait(lock, [this] {
                    return this->stopping_ || !this->tasks_.empty();
                });
                if (this->stopping_ && this->tasks_.empty()) { return; }
                task = std::move(this->tasks_.front());
                this->tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mu_;
    std::condition_variable           cv_;
    bool                              stopping_ = false;
};

} // namespace aoc::ga
