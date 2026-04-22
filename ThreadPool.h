#pragma once
#include <vector>
#include <queue>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include "WorkStealingQueue.h"

class ThreadPool {
    using Task = std::function<void()>;
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkStealingQueue<Task>>> localQueues;
    std::queue<Task> globalQueue;
    std::mutex globalMutex;
    std::condition_variable cv;
    std::atomic<bool> stopFlag{ false };
    size_t numWorkers;
    static thread_local size_t threadIndex;
    static thread_local bool isWorker;

    void workerLoop(size_t myIndex);

public:
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F>
    void enqueue(F&& f);

    void parallelFor(size_t begin, size_t end, const std::function<void(size_t)>& fn);
    size_t workerCount() const;
};
