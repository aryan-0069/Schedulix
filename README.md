# Schedulix — Preemptive Real-Process OS Scheduler & Fair Scheduling Engine

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform: Linux | WSL](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL-orange.svg)](https://ubuntu.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Tests: 80+ Passing](https://img.shields.io/badge/Tests-80%2B%20Passing-brightgreen.svg)](tests.cpp)
[![Architecture: Hybrid Simulation + Real Fork/SIGCONT](https://img.shields.io/badge/Architecture-Hybrid%20Sim%20%2B%20Real%20Fork-purple.svg)](real_scheduler.cpp)

> **Schedulix** is a professional-grade OS CPU scheduling framework and empirical verification engine. It implements six core CPU scheduling algorithms across both a pure software event simulator and an authentic Linux kernel process backend (`fork()` + `SIGSTOP`/`SIGCONT` + `setitimer`/`SIGALRM` preemption), rigorously validating simulated theoretical predictions against hardware wall-clock measurements.

---
## Table of Contents

1. [Project Overview](#project-overview)
2. [Features](#features)
3. [Architecture](#architecture)
4. [File Layout](#file-layout)
5. [Algorithms Implemented](#algorithms-implemented)
6. [Quick Start](#quick-start)
7. [Building](#building)
8. [Running](#running)
9. [Validation Report Explained](#validation-report-explained)
10. [Metrics Reference](#metrics-reference)
11. [Platform Notes](#platform-notes)
12. [Further Reading](#further-reading)

---

## Project Overview

This project demonstrates two complementary views of CPU scheduling:

| Layer | What it does |
|---|---|
| **Simulator** (`simulator.cpp`) | Runs FCFS, SJF, SRTF, Round Robin, MLFQ, and CFS on abstract processes in O(n log n) time, producing Gantt charts and metric tables instantly. Works on any platform. |
| **Real Scheduler** (`real_scheduler.cpp`) | Forks actual Linux child processes, controls them with SIGSTOP/SIGCONT, and uses SIGALRM for quantum-boundary preemption. Measures wall-clock turnaround and waiting times on real workloads. Linux only. |
| **Validator** (`validator.cpp`) | Runs both layers on the identical workload and reports the gap between simulated predictions and real measurements — including per-task delta tables and percentage error. |

The gap between theory and reality is the core learning.  A well-calibrated simulator should predict real TAT within a few percent for burst sizes ≥ 50 ms.

---

## Features

- ✅ **Six scheduling algorithms**: FCFS, SJF, SRTF, Round Robin, MLFQ (3 levels, boost), CFS
- ✅ **Real preemption** via SIGALRM + setitimer at quantum boundaries
- ✅ **Process-accurate CPU measurement** using `CLOCK_PROCESS_CPUTIME_ID` — SIGSTOP pauses do not eat into burst budgets
- ✅ **CFS vruntime + nice-to-weight** table (identical to Linux sched.c)
- ✅ **MLFQ** with configurable levels, allotments, and periodic priority boost (starvation prevention)
- ✅ **Jain's Fairness Index** computed for every algorithm run
- ✅ **Side-by-side comparison table** across all algorithms
- ✅ **Simulator ↔ real validation** with per-metric and per-task error reporting
- ✅ **Gantt chart** (ASCII) for every run
- ✅ **80+ unit and integration tests** with textbook-verified expected answers
- ✅ **Makefile** with convenience `run-*` targets and Linux auto-detection

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        common.hpp                               │
│  Task · Segment · TaskResult · RunSummary                        │
│  niceToWeight() · buildResults() · buildSummary()               │
│  printGantt() · printResultsTable() · printComparison()         │
│  jainsFairnessIndex()                                           │
└──────────────┬──────────────────────────────┬───────────────────┘
               │                              │
    ┌──────────▼──────────┐       ┌──────────▼──────────────────┐
    │   simulator.cpp     │       │    real_scheduler.cpp        │
    │                     │       │                              │
    │  FCFS  SJF  SRTF    │       │  fork() → child parks with   │
    │  RR    MLFQ  CFS    │       │  SIGSTOP                     │
    │                     │       │                              │
    │  Pure event-loop    │       │  SIGALRM → quantum boundary  │
    │  simulation         │       │  SIGSTOP/SIGCONT → preempt   │
    │  (ticks, O(n logn)) │       │  CLOCK_PROCESS_CPUTIME_ID    │
    └──────────┬──────────┘       └──────────┬───────────────────┘
               │                              │
               └──────────────┬───────────────┘
                              │
                   ┌──────────▼──────────┐
                   │    validator.cpp    │
                   │                    │
                   │  Same workload →   │
                   │  both backends →   │
                   │  gap analysis      │
                   └──────────┬─────────┘
                              │
                   ┌──────────▼─────────┐
                   │     tests.cpp      │
                   │  8 suites, 40+     │
                   │  assertions        │
                   └────────────────────┘
```

### Data Flow

```
User input (n, arrival, burst, nice)
        │
        ▼
  std::vector<Task>
        │
   ┌────┴────────────┐
   │    Simulator    │
   │  Algorithm Fn   │──► std::vector<Segment> (timeline)
   └─────────────────┘           │
                                 ▼
                          buildResults()
                                 │
                                 ▼
                      std::vector<TaskResult>
                                 │
                                 ▼
                           buildSummary()
                                 │
                                 ▼
                     printResultsTable() · printGantt()
                     printComparison()  · printValidation()
```

---

## File Layout

```
OS-Scheduler project/
├── common.hpp          ← Shared types, utilities, metrics (header-only)
├── simulator.cpp       ← All 6 algorithms, pure simulation, interactive main
├── real_scheduler.cpp  ← Real Linux process scheduler (fork/signals), interactive main
├── validator.cpp       ← Sim ↔ real comparison engine
├── tests.cpp           ← Unit + integration test suite (60 assertions)
├── Makefile            ← Build system
├── README.md           ← Project overview & architecture
├── GUIDE.md            ← Step-by-step execution & user manual
├── RESULTS.md          ← Comprehensive benchmark results & comparisons
└── CONCEPTS.md         ← Deep-dive: theory, algorithms, OS internals
```

---

## Algorithms Implemented

### 1. FCFS — First Come First Served
- Non-preemptive. Processes run in arrival order.
- Simplest algorithm; suffers from the **convoy effect**.
- `O(n log n)` time (sort by arrival).

### 2. SJF — Shortest Job First (Non-Preemptive)
- Among ready processes, pick the one with the shortest burst.
- **Optimal for minimising average waiting time** among non-preemptive algorithms.
- Requires knowing burst time in advance (impractical in general; approximated with exponential averaging in real OSes).

### 3. SRTF — Shortest Remaining Time First (Preemptive SJF)
- Preemptive version of SJF. On each new arrival, if the new process has a shorter remaining time than the current process, it preempts.
- **Globally optimal** for minimising average waiting time.
- High context-switch overhead.

### 4. Round Robin (RR)
- Each process gets a fixed time quantum `q`.  After the quantum expires it is moved to the back of the ready queue.
- **Fair** — no process waits more than `(n−1)·q` ticks.
- Response time is bounded; turnaround degrades for CPU-bound tasks.
- Quantum trade-off: small `q` → more context switches; large `q` → degenerates to FCFS.

### 5. MLFQ — Multi-Level Feedback Queue
Implements the classic Ousterhout MLFQ with the following rules:

| Rule | Description |
|---|---|
| **R1** | New process enters Level 0 (highest priority) |
| **R2** | If a process exhausts its full allotment at level i, it demotes to level i+1 |
| **R3** | If a process yields before the allotment expires, it stays at the same level |
| **R4** | Every `BOOST_INTERVAL` ticks, **all** processes are moved to Level 0 (prevents starvation) |
| **R5** | Higher-level processes preempt lower-level running processes |

Level quanta: L0=Q, L1=2Q, L2=FCFS (infinite allotment).

### 6. CFS — Completely Fair Scheduler
Mirrors Linux's CFS design:

| Concept | Detail |
|---|---|
| **vruntime** | Virtual runtime = Σ(Δtime · WEIGHT_NICE0 / weight_i). Weighted processes accumulate vruntime more slowly. |
| **Runqueue** | Ordered by vruntime (red-black tree in Linux; `std::set` here). Always pick minimum vruntime. |
| **Slice size** | `(target_latency · weight_i) / total_weight`, clamped to `min_granularity`. |
| **Wakeup** | New/waking process sets vruntime = max(0, min_vruntime − target_latency) so it gets scheduled soon without starving others. |
| **nice mapping** | Uses the exact Linux `sched_prio_to_weight[]` table (nice −20 → 88761, nice 0 → 1024, nice 19 → 15). |

---

## Quick Start

### Simulator (any platform)

```bash
g++ -std=c++17 -O2 -Wall -o scheduler_sim simulator.cpp
./scheduler_sim
```

```
Number of processes: 4
Enter (arrival_time  burst_time  nice_value):
  P0: 0 6 0
  P1: 1 4 0
  P2: 2 2 0
  P3: 3 8 0
Time quantum for RR/MLFQ (default=4): 2
```

### Real Scheduler (Linux only)

```bash
g++ -std=c++17 -O2 -Wall -o scheduler_real real_scheduler.cpp
./scheduler_real --rr --quantum 200
```

```
Number of processes: 3
Enter (arrival_ms  burst_ms  nice):
  P0: 0 500 0
  P1: 0 300 0
  P2: 100 400 0
```

### Validator (Linux only)

```bash
g++ -std=c++17 -O2 -Wall -o validator validator.cpp
./validator --quantum 100
```

### Tests

```bash
g++ -std=c++17 -O2 -Wall -o run_tests tests.cpp
./run_tests
```

---

## Building

### With Make

```bash
make all          # builds everything (real targets skipped on non-Linux)
make run-tests    # build + run unit tests
make run-sim      # build + run simulator with built-in sample data
make run-real     # build + run real scheduler (Linux only)
make run-validator
make clean
make help
```

### Without Make

```bash
# Simulator (all platforms)
g++ -std=c++17 -O2 -Wall -I. -o scheduler_sim simulator.cpp

# Real scheduler (Linux)
g++ -std=c++17 -O2 -Wall -I. -o scheduler_real real_scheduler.cpp

# Validator (Linux)
g++ -std=c++17 -O2 -Wall -I. -o validator validator.cpp

# Tests (all platforms)
g++ -std=c++17 -O2 -Wall -I. -o run_tests tests.cpp
```

---

## Running

### Simulator Output

For each algorithm:
1. **Per-task table** — PID, AT, BT, start time, completion time, turnaround, waiting, response, context switches
2. **Gantt chart** — ASCII timeline
3. **Summary line** — average TAT, WT, RT
4. **Final comparison table** — all six algorithms side-by-side with CPU utilization and Jain's J

### Real Scheduler CLI Flags

| Flag | Description |
|---|---|
| `--fcfs` | Run FCFS only |
| `--rr` | Run Round Robin only |
| `--mlfq` | Run MLFQ only |
| `--cfs` | Run CFS only |
| `--quantum <ms>` | Set time quantum in milliseconds (default: 100 ms) |
| *(no flags)* | Run all four algorithms and compare |

### Recommended Burst Sizes

| Scenario | Min burst |
|---|---|
| Quick demo | 50 ms |
| Meaningful comparison | 200 ms |
| Tight validation | 500 ms |

Bursts below 50 ms may have >10% error due to OS scheduler jitter and signal delivery latency.

---

## Validation Report Explained

```
════════════════════════════════════════════════════════════════════════
  VALIDATION REPORT — Round Robin
════════════════════════════════════════════════════════════════════════
Metric          Simulated    Real         Abs Error    % Error
────────────────────────────────────────────────────────────────────────
Avg TAT         600.00       615.32       15.32        2.55%
Avg WT          300.00       312.18       12.18        4.06%
Avg RT          0.00         8.45         8.45         -
Ctx Switches    6            6            0            0.00%
CPU Util%       100.00       97.23        2.77         2.77%
Jain's J        1.00         0.97         0.03         3.00%
════════════════════════════════════════════════════════════════════════
```

**Typical sources of error:**
- **Signal delivery latency**: SIGALRM arrives a few microseconds late → slice slightly overshoots
- **OS jitter**: The Linux kernel's own scheduling decisions between the parent and children
- **Timer resolution**: `setitimer` has ~1 ms resolution; very short quanta magnify error
- **Fork overhead**: First slice of each process includes fork setup cost (mitigated by pre-forking)

---

## Metrics Reference

| Metric | Formula | Meaning |
|---|---|---|
| **Turnaround Time (TAT)** | CT − AT | Total elapsed time from arrival to completion |
| **Waiting Time (WT)** | TAT − BT | Time spent ready but not on CPU |
| **Response Time (RT)** | ST − AT | Time from arrival to first CPU slice |
| **Context Switches** | Count of PID changes in timeline | Overhead indicator |
| **CPU Utilization** | Busy / Makespan × 100% | Fraction of time CPU is active |
| **Jain's Fairness Index** | (Σxᵢ)² / (n·Σxᵢ²) | 1.0 = perfect fairness, 1/n = worst |
| **Makespan** | last CT − first AT | Total wall clock span of the schedule |

**Jain's Fairness Index xᵢ = BurstᵢI / TATᵢ** (fraction of "useful time" for task i).

---

## Platform Notes

| Component | Platform |
|---|---|
| `simulator.cpp` | Any (Linux, macOS, Windows with MinGW/WSL) |
| `tests.cpp` | Any |
| `real_scheduler.cpp` | **Linux only** — uses `fork()`, `SIGSTOP`, `SIGCONT`, `setitimer`, `CLOCK_PROCESS_CPUTIME_ID` |
| `validator.cpp` | **Linux only** |

The real backend requires:
- `fork()` / `waitpid()` — POSIX
- `SIGSTOP` / `SIGCONT` — POSIX signals
- `setitimer(ITIMER_REAL, ...)` — POSIX timers
- `clock_gettime(CLOCK_PROCESS_CPUTIME_ID, ...)` — POSIX.1b

All are available on any Linux 4.x+ kernel.

---

## Further Reading

See [**CONCEPTS.md**](CONCEPTS.md) for the complete deep-dive covering:
- OS scheduling theory from first principles
- All six algorithm derivations with worked examples
- MLFQ design rationale (Ousterhout et al.)
- CFS internals: vruntime arithmetic, red-black tree, nice-to-weight table
- Real Linux process control: fork, exec, signals, wait
- Timer mechanisms: setitimer, timerfd, SIGALRM
- Validation methodology and error analysis
- Fairness theory: Jain's index, max-min fairness, proportional share
