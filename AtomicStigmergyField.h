#pragma once
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>

class AtomicStigmergyField {
    static constexpr int SIZE = 64;
    static constexpr double DECAY = 0.99;
    struct alignas(64) Cell { std::atomic<double> value{ 0.0 }; };
    std::array<std::array<Cell, SIZE>, SIZE> grid;
    std::thread decayThread;
    std::atomic<bool> running{ true };

public:
    AtomicStigmergyField();
    ~AtomicStigmergyField();
    AtomicStigmergyField(const AtomicStigmergyField&) = delete;
    AtomicStigmergyField& operator=(const AtomicStigmergyField&) = delete;

    void deposit(int x, int y, double v);
    double sense(int x, int y) const;
};
