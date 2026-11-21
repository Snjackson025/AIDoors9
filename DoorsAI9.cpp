// ============================================================
// AIDoors Hardened Core
// Work-Stealing | Cache-Line Optimized | Lock-free PSO | Real-time
// Nicholas Jackson Production Architecture
// ============================================================
// Fixes applied by Muse 2026-11-20 (approved by Saul):
//  - Added missing includes: <functional>, <future>, <iomanip>,
//    <cstdint>, and "Vector7D.h" (Vector7D + CACHE_LINE live there)
//  - Removed duplicate CACHE_LINE definition (Vector7D.h defines it)
//  - Worker loop unwraps std::optional<std::function<...>> from
//    pop()/steal() instead of assigning it to std::function
//  - Removed invalid Particle pad array (negative size); the struct
//    already spans exactly 4 cache lines without it
//  - bestFitness is an aligned double, not a struct: use it directly
//    instead of bestFitness.value
//  - AtomicStigmergyField::deposit uses a CAS loop: std::atomic<double>
//    has no fetch_add in C++17
//  - Door::bifurcate uses PSOEngine::swarmSize() (swarm is private)
//  - Global best now tracks the MINIMUM fitness (Rastrigin min = 0),
//    matching the personal-best minimization
//  - parallelFor completes its promise exactly once (finished counter)
//    instead of a racy counter check that could set it twice
//  - _USE_MATH_DEFINES for M_PI on MSVC; ThreadPool falls back to 1
//    thread if hardware_concurrency() returns 0; parallelFor returns
//    immediately for an empty range
//  - AIDoorsNexus declares the ThreadPool FIRST so it is destroyed
//    last: a Door's loop thread calls pool.parallelFor(), and the old
//    order destroyed the pool while Doors were still stepping, which
//    hung the program on quit after a few seconds of running
// ============================================================

#ifdef _MSC_VER
#define _USE_MATH_DEFINES
#endif

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <random>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <queue>
#include <condition_variable>
#include <array>
#include <cmath>
#include <sstream>
#include <optional>
#include <functional>
#include <future>
#include <iomanip>
#include <cstdint>
#include <immintrin.h>
#include "Vector7D.h"

// ============================================================
// CACHE-LINE OPTIMIZATION
// (CACHE_LINE is defined in Vector7D.h)
// NOTE: MSVC rejects alignas in a type alias ("using Aligned =
// alignas(...) T"), so alignment is applied directly on the
// member instead (see Particle::bestFitness below).
// ============================================================

// ============================================================
// WORK-STEALING QUEUE (Chase-Lev Deque)
// Based on Nhat Minh Lê et al. PPoPP 2013 [^29^][^36^]
// ============================================================

template<typename T>
class WorkStealingQueue {
    struct CircularArray {
        std::vector<std::optional<T>> buffer;
        size_t mask;

        CircularArray(size_t capacity) : buffer(capacity), mask(capacity - 1) {}

        size_t size() const { return buffer.size(); }

        std::optional<T>& operator[](size_t i) { return buffer[i & mask]; }
    };

    alignas(CACHE_LINE) std::atomic<size_t> top{0};
    alignas(CACHE_LINE) std::atomic<size_t> bottom{0};
    alignas(CACHE_LINE) std::atomic<CircularArray*> array{nullptr};

public:
    WorkStealingQueue(size_t initialCapacity = 1024) {
        array.store(new CircularArray(initialCapacity), std::memory_order_relaxed);
    }

    ~WorkStealingQueue() {
        delete array.load(std::memory_order_relaxed);
    }

    // Owner: push to bottom
    void push(T item) {
        size_t b = bottom.load(std::memory_order_relaxed);
        size_t t = top.load(std::memory_order_acquire);
        CircularArray* a = array.load(std::memory_order_relaxed);

        if(b - t > a->size() - 1) {
            // Resize needed
            CircularArray* newArray = new CircularArray(a->size() * 2);
            for(size_t i = t; i < b; ++i) {
                (*newArray)[i] = (*a)[i];
            }
            array.store(newArray, std::memory_order_release);
            delete a;
            a = newArray;
        }

        (*a)[b] = item;
        std::atomic_thread_fence(std::memory_order_release);
        bottom.store(b + 1, std::memory_order_relaxed);
    }

    // Owner: pop from bottom
    std::optional<T> pop() {
        size_t b = bottom.load(std::memory_order_relaxed) - 1;
        CircularArray* a = array.load(std::memory_order_relaxed);
        bottom.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        size_t t = top.load(std::memory_order_relaxed);

        if(t <= b) {
            std::optional<T> item = (*a)[b];
            if(t == b) {
                // Last item - race with thieves
                if(!top.compare_exchange_strong(t, t + 1,
                                                  std::memory_order_seq_cst,
                                                  std::memory_order_relaxed)) {
                    bottom.store(b + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                bottom.store(b + 1, std::memory_order_relaxed);
            }
            return item;
        } else {
            bottom.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
    }

    // Thieves: steal from top
    std::optional<T> steal() {
        size_t t = top.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        size_t b = bottom.load(std::memory_order_acquire);

        if(t < b) {
            CircularArray* a = array.load(std::memory_order_consume);
            std::optional<T> item = (*a)[t];
            if(top.compare_exchange_strong(t, t + 1,
                                          std::memory_order_seq_cst,
                                          std::memory_order_relaxed)) {
                return item;
            }
        }
        return std::nullopt;
    }
};

// ============================================================
// THREAD POOL WITH WORK-STEALING
// Based on PAPSO fork-join model [^28^]
// ============================================================

class ThreadPool {
    std::vector<std::thread> workers;
    std::vector<WorkStealingQueue<std::function<void()>>> localQueues;
    std::queue<std::function<void()>> globalQueue;
    std::mutex globalMutex;
    std::condition_variable cv;
    std::atomic<bool> stop{false};
    std::atomic<size_t> idleCount{0};

    thread_local static WorkStealingQueue<std::function<void()>>* localQueue;

public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency())
        : localQueues(numThreads == 0 ? 1 : numThreads) {

        size_t n = localQueues.size();
        for(size_t i = 0; i < n; ++i) {
            workers.emplace_back([this, i] {
                localQueue = &localQueues[i];

                while(!stop) {
                    std::function<void()> task;

                    // Try local queue first
                    if(auto opt = localQueue->pop()) {
                        (*opt)();
                        continue;
                    }

                    // Try stealing from others
                    bool stolen = false;
                    for(size_t j = 0; j < localQueues.size(); ++j) {
                        if(j == i) continue;
                        if(auto opt = localQueues[j].steal()) {
                            (*opt)();
                            stolen = true;
                            break;
                        }
                    }
                    if(stolen) {
                        continue;
                    }

                    // Try global queue
                    {
                        std::unique_lock<std::mutex> lock(globalMutex);
                        if(!globalQueue.empty()) {
                            task = std::move(globalQueue.front());
                            globalQueue.pop();
                        } else {
                            idleCount++;
                            cv.wait(lock, [this] { return stop || !globalQueue.empty(); });
                            idleCount--;
                            if(stop) return;
                            if(!globalQueue.empty()) {
                                task = std::move(globalQueue.front());
                                globalQueue.pop();
                            }
                        }
                    }
                    if(task) task();
                }
            });
        }
    }

    template<typename F>
    void enqueue(F&& f) {
        if(localQueue) {
            localQueue->push(std::forward<F>(f));
        } else {
            std::lock_guard<std::mutex> lock(globalMutex);
            globalQueue.push(std::forward<F>(f));
        }
        cv.notify_one();
    }

    void parallelFor(size_t begin, size_t end, std::function<void(size_t)> fn) {
        if(end <= begin) return;

        std::atomic<size_t> counter{begin};
        std::atomic<size_t> finished{0};
        std::promise<void> promise;
        auto future = promise.get_future();

        size_t numTasks = std::min(end - begin, workers.size());

        for(size_t t = 0; t < numTasks; ++t) {
            enqueue([&, t] {
                while(true) {
                    size_t i = counter.fetch_add(1, std::memory_order_relaxed);
                    if(i >= end) break;
                    fn(i);
                }
                // Exactly one task completes the promise
                if(finished.fetch_add(1, std::memory_order_acq_rel) + 1 == numTasks) {
                    promise.set_value();
                }
            });
        }

        future.wait();
    }

    ~ThreadPool() {
        stop = true;
        cv.notify_all();
        for(auto& w : workers) w.join();
    }
};

thread_local WorkStealingQueue<std::function<void()>>* ThreadPool::localQueue = nullptr;

// ============================================================
// LOCK-FREE PSO ENGINE
// Cache-aligned particles, atomic global best
// Based on PAPSO nonblocking protocol [^28^]
// ============================================================

struct alignas(CACHE_LINE) Particle {
    Vector7D position;
    Vector7D velocity;
    Vector7D bestPosition;
    alignas(CACHE_LINE) double bestFitness;
};

static_assert(sizeof(Particle) == CACHE_LINE * 4, "Particle spans cache lines");

class PSOEngine {
    std::vector<Particle> swarm;

    // Global best - separate cache lines for atomic operations
    alignas(CACHE_LINE) std::atomic<double> globalBestFitness;
    alignas(CACHE_LINE) Vector7D globalBestPosition;
    alignas(CACHE_LINE) std::mutex bestMutex;

    static constexpr double W = 0.729;
    static constexpr double C1 = 1.49445;
    static constexpr double C2 = 1.49445;

    ThreadPool& pool;

public:
    PSOEngine(size_t count, ThreadPool& p) : globalBestFitness(1e308), pool(p) {
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> dist(-1.0, 1.0);

        swarm.resize(count);

        for(auto& p : swarm) {
            for(int i = 0; i < 7; ++i) {
                p.position.data[i] = dist(rng);
                p.velocity.data[i] = dist(rng) * 0.1;
            }
            p.bestPosition = p.position;
            p.bestFitness = fitness(p.position);
            updateGlobalBest(p.bestFitness, p.bestPosition);
        }
    }

    double fitness(const Vector7D& v) {
        // Rastrigin-like function for testing convergence
        double sum = 0;
        for(int i = 0; i < 7; ++i) {
            sum += v.data[i] * v.data[i] - 10 * std::cos(2 * M_PI * v.data[i]);
        }
        return 10 * 7 + sum;
    }

    void updateGlobalBest(double fitness, const Vector7D& pos) {
        double current = globalBestFitness.load(std::memory_order_relaxed);
        while(fitness < current && !globalBestFitness.compare_exchange_weak(
            current, fitness, std::memory_order_release, std::memory_order_relaxed)) {}

        if(fitness < current) {
            std::lock_guard<std::mutex> lock(bestMutex);
            if(fitness < globalBestFitness.load(std::memory_order_acquire)) {
                globalBestPosition = pos;
            }
        }
    }

    void step() {
        pool.parallelFor(0, swarm.size(), [this](size_t i) {
            auto& p = swarm[i];
            std::mt19937 rng(std::random_device{}() + i);
            std::uniform_real_distribution<double> r(0.0, 1.0);

            // Vectorized update would go here with AVX-512

            for(int d = 0; d < 7; ++d) {
                double r1 = r(rng), r2 = r(rng);

                p.velocity.data[d] = W * p.velocity.data[d] +
                                    C1 * r1 * (p.bestPosition.data[d] - p.position.data[d]) +
                                    C2 * r2 * (globalBestPosition.data[d] - p.position.data[d]);

                p.velocity.data[d] = std::clamp(p.velocity.data[d], -0.5, 0.5);
                p.position.data[d] += p.velocity.data[d];
                p.position.data[d] = std::clamp(p.position.data[d], -5.12, 5.12);
            }

            double f = fitness(p.position);
            if(f < p.bestFitness) {
                p.bestFitness = f;
                p.bestPosition = p.position;
                updateGlobalBest(f, p.position);
            }
        });
    }

    size_t swarmSize() const { return swarm.size(); }
    Vector7D getGlobalBest() const { return globalBestPosition; }
    double getBestFitness() const { return globalBestFitness.load(); }
};

// ============================================================
// ATOMIC STIGMERGY FIELD
// Lock-free grid with relaxed memory ordering
// ============================================================

class AtomicStigmergyField {
    static constexpr int SIZE = 64;
    static constexpr double DECAY = 0.99;

    // Each cell padded to prevent false sharing
    struct alignas(CACHE_LINE) Cell {
        std::atomic<double> value{0.0};
    };

    std::array<std::array<Cell, SIZE>, SIZE> grid;
    std::thread decayThread;
    std::atomic<bool> running{true};

public:
    AtomicStigmergyField() {
        decayThread = std::thread([this] {
            while(running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                for(auto& row : grid) {
                    for(auto& cell : row) {
                        double v = cell.value.load(std::memory_order_relaxed);
                        if(v > 0.001) {
                            cell.value.store(v * DECAY, std::memory_order_relaxed);
                        }
                    }
                }
            }
        });
    }

    ~AtomicStigmergyField() {
        running = false;
        if(decayThread.joinable()) decayThread.join();
    }

    void deposit(int x, int y, double v) {
        x = std::clamp(x, 0, SIZE - 1);
        y = std::clamp(y, 0, SIZE - 1);
        // std::atomic<double> has no fetch_add in C++17: CAS loop
        double old = grid[x][y].value.load(std::memory_order_relaxed);
        double desired;
        do {
            desired = old + v;
        } while(!grid[x][y].value.compare_exchange_weak(old, desired,
                    std::memory_order_relaxed, std::memory_order_relaxed));
    }

    double sense(int x, int y) const {
        x = std::clamp(x, 0, SIZE - 1);
        y = std::clamp(y, 0, SIZE - 1);
        return grid[x][y].value.load(std::memory_order_relaxed);
    }
};

// ============================================================
// DOOR (Resource Container)
// ============================================================

class Door {
    std::string id;
    std::unique_ptr<PSOEngine> pso;
    std::unique_ptr<AtomicStigmergyField> field;
    std::atomic<bool> running{true};
    std::thread loopThread;
    ThreadPool& pool;

    // Statistics
    std::atomic<uint64_t> iterations{0};
    std::chrono::steady_clock::time_point startTime;

public:
    Door(const std::string& id_, size_t swarmSize, ThreadPool& p)
        : id(id_), pool(p) {

        field = std::make_unique<AtomicStigmergyField>();
        pso = std::make_unique<PSOEngine>(swarmSize, pool);

        startTime = std::chrono::steady_clock::now();

        loopThread = std::thread([this] {
            while(running) {
                pso->step();

                Vector7D best = pso->getGlobalBest();
                int gx = static_cast<int>((best.data[0] + 5.12) * 6.25);
                int gy = static_cast<int>((best.data[1] + 5.12) * 6.25);
                field->deposit(gx, gy, 1.0 / (1.0 + pso->getBestFitness()));

                iterations++;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });
    }

    ~Door() {
        running = false;
        if(loopThread.joinable()) loopThread.join();
    }

    void status() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();

        std::cout << "[" << std::setw(20) << id << "] "
                  << "Iter: " << std::setw(8) << iterations.load()
                  << " | Best: " << std::setw(10) << std::fixed << std::setprecision(6)
                  << pso->getBestFitness()
                  << " | Pos: [" << std::setprecision(2) << pso->getGlobalBest().data[0]
                  << ", " << pso->getGlobalBest().data[1] << "...]"
                  << " | Uptime: " << elapsed << "s\n";
    }

    std::shared_ptr<Door> bifurcate(const std::string& newId, ThreadPool& pool) {
        // Create new door with same config but forked PSO state
        return std::make_shared<Door>(newId, pso->swarmSize(), pool);
    }
};

// ============================================================
// NEXUS
// ============================================================

class AIDoorsNexus {
    // pool is declared first so it is destroyed LAST: the Doors' loop
    // threads call pool.parallelFor(), so the pool must outlive them.
    // (Members are destroyed in reverse declaration order.)
    std::unique_ptr<ThreadPool> pool;
    std::map<std::string, std::shared_ptr<Door>> doors;
    std::mutex doorsMutex;

public:
    AIDoorsNexus() : pool(std::make_unique<ThreadPool>()) {}

    void create(const std::string& id, size_t swarmSize = 64) {
        std::lock_guard<std::mutex> lock(doorsMutex);
        doors[id] = std::make_shared<Door>(id, swarmSize, *pool);
    }

    void destroy(const std::string& id) {
        std::lock_guard<std::mutex> lock(doorsMutex);
        doors.erase(id);
    }

    void list() {
        std::lock_guard<std::mutex> lock(doorsMutex);
        std::cout << "\n=== ACTIVE DOORS ===\n";
        for(const auto& [id, door] : doors) {
            door->status();
        }
    }

    void bifurcate(const std::string& id, const std::string& newId) {
        std::lock_guard<std::mutex> lock(doorsMutex);
        auto it = doors.find(id);
        if(it != doors.end()) {
            doors[newId] = it->second->bifurcate(newId, *pool);
        }
    }
};

// ============================================================
// CLI
// ============================================================

int main() {
    AIDoorsNexus nexus;
    std::string line;

    std::cout << "AIDoors Hardened Core (Work-Stealing PSO)\n";
    std::cout << "Commands: create <id> [swarm] | destroy <id> | list | bifurcate <id> <newid> | quit\n\n";

    while(std::cout << "> " && std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if(cmd == "quit") break;
        else if(cmd == "create") {
            std::string id;
            size_t swarm = 64;
            iss >> id >> swarm;
            if(!id.empty()) nexus.create(id, swarm);
        }
        else if(cmd == "destroy") {
            std::string id; iss >> id;
            nexus.destroy(id);
        }
        else if(cmd == "list") {
            nexus.list();
        }
        else if(cmd == "bifurcate") {
            std::string id, newId;
            iss >> id >> newId;
            nexus.bifurcate(id, newId);
        }
    }

    return 0;
}
