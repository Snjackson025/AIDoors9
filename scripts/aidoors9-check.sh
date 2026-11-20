#!/bin/sh
# AIDoors9 local check: build the full project (all .cpp files, as the
# .vcxproj does) with g++ -std=c++17 and run a scripted smoke test.
set -eu
cd "$(dirname "$0")/.."

g++ -std=c++17 -Wall -Wextra -o /tmp/aidoors9_check_build \
  AtomicStigmergyField.cpp Door.cpp DoorsAI9.cpp Padded.cpp \
  PSOEngine.cpp ThreadPool.cpp Vector7D.cpp WorkStealingQueue.cpp

printf 'create test1 16\nlist\nbifurcate test1 test2\nlist\ndestroy test1\nlist\nquit\n' \
  | timeout 20 /tmp/aidoors9_check_build > /tmp/aidoors9_check_run.log

grep -q "AIDoors Hardened Core" /tmp/aidoors9_check_run.log
grep -q "ACTIVE DOORS" /tmp/aidoors9_check_run.log
grep -q "test1" /tmp/aidoors9_check_run.log
grep -q "test2" /tmp/aidoors9_check_run.log

echo "AIDoors9 check: PASS"
