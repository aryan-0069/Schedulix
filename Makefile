# ─── OS Scheduler Project — Makefile ─────────────────────────────────────────
#
#  Targets:
#    make all          — build all binaries
#    make simulator    — pure simulation engine
#    make real         — real Linux process scheduler (Linux only)
#    make validator    — sim ↔ real validator (Linux only)
#    make tests        — unit + integration test binary
#    make run-sim      — build + run simulator with sample workload
#    make run-tests    — build + run tests
#    make clean        — remove build artefacts

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I.
LDFLAGS  :=


simulator: simulator.cpp common.hpp
	$(CXX) $(CXXFLAGS) -o scheduler_sim simulator.cpp
scheduler: scheduler.cpp
	$(CXX) $(CXXFLAGS) -o scheduler scheduler.cpp
