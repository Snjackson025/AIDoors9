# AIDoors9 workflow

## Program

`DoorsAI9.cpp` is the program: the AIDoors Hardened Core — a work-stealing
thread pool, lock-free particle-swarm optimizer (PSO) in 7 dimensions, an
atomic stigmergy field, Doors, and a Nexus with a CLI (`create`, `destroy`,
`list`, `bifurcate`, `quit`). It needs `Vector7D.h` and C++17.

The repo's older split headers (`Door.h`, `PSOEngine.h`, `ThreadPool.h`,
`WorkStealingQueue.h`, `AtomicStigmergyField.h`, `Padded.h`) and their
empty `.cpp` files remain in the repo but are not used by the program.
`AIDoors9.vcxproj` sets `<LanguageStandard>stdcpp17</LanguageStandard>`
in all four configurations, because the program uses C++17
(`std::optional`, `std::clamp`, structured bindings).

## Local check

```sh
sh scripts/aidoors9-check.sh
```

Builds all `.cpp` files with `g++ -std=c++17 -Wall -Wextra`, then runs a
scripted session (`create`, `list`, `bifurcate`, `destroy`, `quit`) and
requires the banner, `ACTIVE DOORS`, and both door names in the output.
Exits non-zero on any failure.

## GitHub Actions

`.github/workflows/build-and-test.yml` builds the solution with MSBuild
(Debug x64) and runs the same smoke test against `x64\Debug\AIDoors9.exe`.
It fails if the build fails, the exe is missing, the exit code is non-zero,
or the expected output is missing. Muse cannot push workflow files, so the
file is added or replaced through the GitHub web UI.
