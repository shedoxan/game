#pragma once
#include <functional>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);

    ~ThreadPool();

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result_t<F, Args...>>
    {
        using RetT = typename std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<RetT()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<RetT> res = task->get_future();
        {
            std::unique_lock lock(queueMutex);
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    void workerThread();

    std::vector<std::thread> workers;                    // Вектор рабочих потоков.
    std::queue<std::function<void()>> tasks;             // Очередь задач. std::function<void()> - универсальный контейнер для хранения вызываемых объектов с сигнатурой, которая не принимает объектов и возвращает void 
    std::mutex queueMutex;                               // Мьютекс для защиты очереди задач. 
    std::condition_variable condition;                   // Условная переменная для уведомления рабочих потоков. Это примитив синхронизации, который позволяет потокам ждать наступления определенного условия. 
    std::atomic<bool> stop;                              // Флаг, сигнализирующий о необходимости остановки пула. Гарантирует, что операции чтения и записи значения переменной выполняются атомарность, т.е без возникновения race condition.
};
