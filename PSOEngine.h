#pragma once
#include <vector>
#include <shared_mutex>
#include <memory>
#include "Vector7D.h"
#include "ThreadPool.h"

struct alignas(CACHE_LINE) Particle {
    Vector7D position;
    Vector7D velocity;
    Vector7D bestPosition;
    double bestFitness;
    Particle() : bestFitness(1e308) {}
};

class PSOEngine {
    std::vector<Particle> swarm_;
    mutable std::shared_mutex bestMutex;
    Vector7D globalBestPosition_;
    double globalBestFitness_;
    ThreadPool& pool;

public:
    explicit PSOEngine(size_t count, ThreadPool& p);
    static double fitness(const Vector7D& v);
    void step();
    void tryUpdateGlobalBest(double f, const Vector7D& pos);
    Vector7D getGlobalBestPosition() const;
    double getGlobalBestFitness() const;
    size_t swarmSize() const { return swarm_.size(); }
};
