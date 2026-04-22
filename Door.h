#pragma once
#include <memory>
#include <atomic>
#include <string>
#include <thread>
#include <chrono>
#include "PSOEngine.h"
#include "AtomicStigmergyField.h"
#include "ThreadPool.h"
#include "Vector7D.h"

class Door {
    std::string id;
    std::unique_ptr<PSOEngine> pso;
    std::unique_ptr<AtomicStigmergyField> field;
    std::atomic<bool> running{ true };
    std::thread loopThread;
    ThreadPool& pool;
    std::atomic<uint64_t> iterations{ 0 };
    std::chrono::steady_clock::time_point startTime;

public:
    Door(const std::string& id_, size_t swarmSize, ThreadPool& p);
    ~Door();
    Door(const Door&) = delete;
    Door& operator=(const Door&) = delete;

    void status() const;
    size_t swarmSize() const;
    std::shared_ptr<Door> bifurcate(const std::string& newId, ThreadPool& p) const;
};
