#include "threadpool.h"

ThreadPool::ThreadPool(size_t numThreads) : stop(false) {
    workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            this->workerThread();
            });
    }
}

ThreadPool::~ThreadPool() {
    // Устанавливаем флаг остановки для потоков
    stop.store(true);
    condition.notify_all();
    // Ожидаем завершения всех потоков
    for (auto& thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}


void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            // Ожидаем, пока не появится задача или не будет установлен флаг остановки
            condition.wait(lock, [this] {
                return stop.load() || !tasks.empty();
                });
            // Если установлен флаг остановки и очередь пуста, завершаем работу потока
            if (stop.load() && tasks.empty()) {
                return;
            }
            task = std::move(tasks.front());
            tasks.pop();
        }
        // Выполняем задачу
        task();
    }
}
