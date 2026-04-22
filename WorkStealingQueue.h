#pragma once
#include <vector>
#include <optional>
#include <atomic>
#include <mutex>

template <typename T>
class WorkStealingQueue {
    struct CircularArray {
        std::vector<std::optional<T>> buffer;
        size_t mask;
        explicit CircularArray(size_t capacity)
            : buffer(capacity), mask(capacity - 1) {
        }
        size_t size() const { return buffer.size(); }
        std::optional<T>& operator[](size_t i) { return buffer[i & mask]; }
    };

    alignas(CACHE_LINE) std::atomic<size_t> top{ 0 };
    alignas(CACHE_LINE) std::atomic<size_t> bottom{ 0 };
    alignas(CACHE_LINE) std::atomic<CircularArray*> array{ nullptr };

    std::vector<CircularArray*> retired;
    std::mutex retiredMutex;

public:
    explicit WorkStealingQueue(size_t initialCapacity = 1024);
    ~WorkStealingQueue();

    WorkStealingQueue(const WorkStealingQueue&) = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

    void push(T item);
    std::optional<T> pop();
    std::optional<T> steal();
    bool empty() const;
};
