# AIDoors9

**AIDoors Hardened Core — doors that run their own particle-swarm
optimizers on a work-stealing thread pool.**

## What it does

- **WorkStealingQueue** — Chase-Lev work-stealing deque
- **ThreadPool** — work-stealing thread pool with parallel-for
- **PSOEngine** — lock-free particle swarm optimization
- **AtomicStigmergyField** — lock-free pheromone-style field
- **Door** — a door that runs a swarm, with bifurcation (splitting one
  door into two)
- **AIDoorsNexus** — owns the doors and the pool

Commands: `create <id> [swarm]` | `destroy <id>` | `list` |
`bifurcate <id> <newid>` | `quit`. Create two doors and watch their
best fitness converge toward 0.

## Build and run

Requires C++17. With Visual Studio, open the solution and build
Debug x64. Or with g++:

```sh
g++ -std=c++17 -Wall -Wextra -o AIDoors9 DoorsAI9.cpp Vector7D.cpp
./AIDoors9
```

## Continuous integration

`.github/workflows/build-and-test.yml` builds Debug x64 with MSBuild and
runs a smoke test (create a door, list it, quit), failing unless the
output shows the banner, the active doors, and the created door. It
also runs daily at 06:00 UTC.
