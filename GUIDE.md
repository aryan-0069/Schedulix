# OS Scheduler — Complete User & Execution Guide (GUIDE.md)

> A step-by-step operational handbook explaining **what to run**, **which binary to choose**, **how to compile**, **how to pass inputs**, and **how to interpret results** across Windows, Linux, and macOS.

---

## Table of Contents

1. [Binary & Component Selector (Which One Do I Run?)](#1-binary--component-selector)
2. [Prerequisites & Environment Setup](#2-prerequisites--environment-setup)
3. [Building the Project](#3-building-the-project)
4. [Running the Software Simulator (`scheduler_sim`)](#4-running-the-software-simulator)
5. [Running the Test Suite (`run_tests`)](#5-running-the-test-suite)
6. [Running the Real Linux Scheduler (`scheduler_real`)](#6-running-the-real-linux-scheduler)
7. [Running the Simulator ↔ Real Validator (`validator`)](#7-running-the-simulator--real-validator)
8. [Copy-Paste Ready Workload Examples](#8-copy-paste-ready-workload-examples)
9. [Troubleshooting & Frequently Asked Questions](#9-troubleshooting--faqs)

---

## 1. Binary & Component Selector

This project contains four distinct programs. Use this guide to pick the right one for your goal:

| Binary Name | Source File | Platform | Purpose | When to Use |
|---|---|:---:|---|---|
| `scheduler_sim` (`.exe`) | `simulator.cpp` | **All** (Windows, Linux, macOS) | Pure discrete-event scheduling engine. Runs all 6 algorithms (FCFS, SJF, SRTF, RR, MLFQ, CFS) in milliseconds. | Use this for algorithmic exploration, textbook homework examples, instant ASCII Gantt charts, and comparison tables. |
| `run_tests` (`.exe`) | `tests.cpp` | **All** (Windows, Linux, macOS) | 60-assertion test suite validating textbook invariants, MLFQ level demotion/boost, and CFS fairness. | Run this to verify code correctness and algorithm invariants. |
| `scheduler_real` | `real_scheduler.cpp` | **Linux only** (or Windows via WSL) | Real POSIX scheduler using `fork()`, `SIGSTOP`, `SIGCONT`, `setitimer`, and `CLOCK_PROCESS_CPUTIME_ID`. | Use this to see actual operating system processes preempted at real time-slice boundaries. |
| `validator` | `validator.cpp` | **Linux only** (or Windows via WSL) | Automated comparative engine that runs identical workloads through both the simulator and real Linux backend. | Use this to measure the gap between theoretical calculations and real OS execution (error analysis). |

---

## 2. Prerequisites & Environment Setup

### On Windows
- **For Simulator & Tests**: Any standard C++17 compiler (MinGW-w64, MSYS2, or Visual Studio `cl.exe`).
  - Verify compiler:
    ```powershell
    g++ --version
    ```
- **For Real Scheduler & Validator**: Install WSL (Windows Subsystem for Linux):
  ```powershell
  wsl --install
  ```
  Once inside WSL (Ubuntu), run `sudo apt update && sudo apt install -y build-essential`.

### On Linux / macOS
- `g++` supporting C++17 or newer.
- `make` (optional, for convenience targets).
  ```bash
  sudo apt install -y build-essential   # Debian/Ubuntu
  sudo dnf install -y gcc-c++ make      # Fedora/RHEL
  brew install gcc make                 # macOS
  ```

---

## 3. Building the Project

### Method A: With Make (Recommended on Linux / WSL)

```bash
make all           # Compiles all available binaries for your platform
make simulator     # Compiles scheduler_sim
make tests         # Compiles run_tests
make real          # Compiles scheduler_real (Linux only)
make validator     # Compiles validator (Linux only)
make clean         # Removes all compiled binaries and object files
make help          # Displays all available targets
```

### Method B: Direct Compilation with g++ (Any Platform)

```bash
# 1. Simulator (Cross-platform)
g++ -std=c++17 -O2 -Wall -Wextra -I. -o scheduler_sim simulator.cpp

# 2. Test Suite (Cross-platform)
g++ -std=c++17 -O2 -Wall -Wextra -I. -o run_tests tests.cpp

# 3. Real Scheduler (Linux / WSL)
g++ -std=c++17 -O2 -Wall -Wextra -I. -o scheduler_real real_scheduler.cpp

# 4. Validator (Linux / WSL)
g++ -std=c++17 -O2 -Wall -Wextra -I. -o validator validator.cpp
```
*(On Windows PowerShell, replace output names with `.exe`, e.g., `-o scheduler_sim.exe`)*.

---

## 4. Running the Software Simulator

The simulator (`scheduler_sim`) runs all 6 scheduling algorithms on the exact same task set and outputs per-task tables, Gantt charts, and a side-by-side comparison table.

### Interactive Mode

Run the binary directly and answer the prompts:
```bash
./scheduler_sim          # Linux / macOS
.\scheduler_sim.exe      # Windows
```

#### Step-by-Step Input Format:
1. **Number of processes**: Enter an integer $\ge 1$ (e.g., `4`).
2. **Process attributes**: For each process $P_i$, input three integers:
   $$\text{arrival\_time} \quad \text{burst\_time} \quad \text{nice\_value}$$
   - `arrival_time`: Ticks when process enters the ready queue ($\ge 0$).
   - `burst_time`: Total CPU ticks required to complete ($> 0$).
   - `nice_value`: Priority from `-20` (highest) to `19` (lowest). Use `0` for default.
3. **Time quantum**: Time slice for Round Robin and base level of MLFQ (default `4`).

### Piped / Non-Interactive Mode (Instant Execution)

You can pass inputs directly through standard input without typing interactively:

#### On Windows (PowerShell):
```powershell
"4`n0 6 0`n1 4 0`n2 2 0`n3 8 0`n2" | .\scheduler_sim.exe
```

#### On Linux / WSL (Bash):
```bash
printf "4\n0 6 0\n1 4 0\n2 2 0\n3 8 0\n2\n" | ./scheduler_sim
```

---

## 5. Running the Test Suite

The test suite runs 8 specialized test suites containing 60 test assertions, validating textbook answers, mathematical invariants, and boundary conditions.

```bash
./run_tests              # Linux / macOS / WSL
.\run_tests.exe          # Windows PowerShell
```

### What gets tested:
- **Suite 1 (FCFS)**: Arrival ordering, convoy handling, CPU utilization.
- **Suite 2 (SJF)**: Shortest job ordering, non-preemptive turnaround.
- **Suite 3 (SRTF)**: Dynamic arrival preemption, minimal waiting time verification.
- **Suite 4 (Round Robin)**: Quantum expiration, multi-pass queue rotation, response time.
- **Suite 5 (MLFQ Invariants)**:
  - New tasks admitted at Level 0.
  - Long tasks demoted upon quantum expiration.
  - Periodic priority boost resetting all tasks back to Level 0 (starvation prevention).
- **Suite 6 (CFS Fairness)**: Virtual runtime progression, equal-weight symmetry, and proportional CPU allocation for high-weight tasks.
- **Suite 7 (Jain's Fairness Index)**: Bounds testing ($1/n \le J \le 1.0$).
- **Suite 8 (niceToWeight)**: Accurate clamping and matching Linux kernel `sched_prio_to_weight` constants.

---

## 6. Running the Real Linux Scheduler

> **Note**: This component requires the Linux kernel for `fork()`, `SIGSTOP`/`SIGCONT`, `setitimer`, and `CLOCK_PROCESS_CPUTIME_ID`. Run this inside **Linux or WSL**.

```bash
./scheduler_real [FLAGS]
```

### Available CLI Flags:

| Flag | Purpose | Example |
|---|---|---|
| *(no flags)* | Runs all algorithms sequentially and prints full comparison | `./scheduler_real` |
| `--rr` | Run Round Robin only | `./scheduler_real --rr` |
| `--mlfq` | Run Multi-Level Feedback Queue only | `./scheduler_real --mlfq` |
| `--cfs` | Run Completely Fair Scheduler only | `./scheduler_real --cfs` |
| `--fcfs` | Run First-Come First-Served only | `./scheduler_real --fcfs` |
| `--quantum <ms>` | Set time slice in milliseconds (default: 100 ms) | `./scheduler_real --rr --quantum 50` |

### Recommended Workload Rules for Real Mode:
- **Burst times**: Use milliseconds ($\ge 50$ ms, recommended $200–1000$ ms).
- **Quantum**: Use $50–200$ ms so timer overhead and signal delivery latency remain $< 2\%$ of the total runtime.

### Example Run (Round Robin with 100ms quantum):
```bash
printf "3\n0 300 0\n0 200 0\n50 250 0\n" | ./scheduler_real --rr --quantum 100
```

---

## 7. Running the Simulator ↔ Real Validator

The validator evaluates the fidelity of the simulation by running an identical workload on both backends and producing a detailed percentage-error breakdown:

```bash
./validator [--quantum <ms>]
```

### Example:
```bash
printf "3\n0 300 0\n50 200 0\n100 400 0\n" | ./validator --quantum 100
```

### Expected Output Report:
```
════════════════════════════════════════════════════════════════════════
  VALIDATION REPORT — Round Robin
════════════════════════════════════════════════════════════════════════
Metric          Simulated    Real         Abs Error    % Error
────────────────────────────────────────────────────────────────────────
Avg TAT         600.00       614.28       14.28        2.38%
Avg WT          300.00       311.15       11.15        3.72%
Avg RT          50.00        51.80        1.80         3.60%
Ctx Switches    6            6            0            0.00%
CPU Util%       100.00       98.12        1.88         1.88%
Jain's J        0.98         0.97         0.01         1.02%
════════════════════════════════════════════════════════════════════════
```

---

## 8. Copy-Paste Ready Workload Examples

### Workload 1: Classic Textbook Mixed Benchmark
```
4
0 6 0
1 4 0
2 2 0
3 8 0
2
```
*Tests how algorithms handle staggered arrivals and varying burst lengths ($q=2$).*

---

### Workload 2: Priority Differentiation (CFS Test)
```
3
0 100 -10
0 100 0
0 100 10
20
```
*P0 has high priority (nice -10), P1 has normal priority (nice 0), P2 has low priority (nice 10). Demonstrates CFS proportional CPU share.*

---

### Workload 3: Eliminating the Convoy Effect
```
4
0 100 0
15 4 0
30 4 0
45 4 0
4
```
*P0 is a massive CPU hog arriving first; P1, P2, P3 are quick interactive bursts arriving shortly after. Demonstrates how MLFQ, SRTF, and CFS rescue interactive tasks.*

---

## 9. Troubleshooting & FAQs

### Q1: I get `real_scheduler: Linux-only component` on Windows.
**Answer**: `scheduler_real` and `validator` directly manipulate Linux process structures (`fork()`, POSIX signals `SIGSTOP`/`SIGCONT`, `nanosleep`, and POSIX timers). On Windows, use **WSL** (Windows Subsystem for Linux) to run the real process scheduler. For all algorithmic simulations and unit tests, `scheduler_sim.exe` and `run_tests.exe` run natively on Windows.

### Q2: Why does real-mode CPU utilization show 98% instead of 100%?
**Answer**: In real operating systems, context switching between child processes incurs microsecond-level costs (signal generation, kernel context save/restore, and thread dispatch). The simulator assumes instantaneous zero-cost switches, so real measurements reflect actual physical overhead.

### Q3: How do I change the MLFQ boost timer?
**Answer**: In [common.hpp](file:///c:/OS-Scheduler%20project/common.hpp), adjust `MLFQ_BOOST_INTERVAL` (default is 100 ticks). In [real_scheduler.cpp](file:///c:/OS-Scheduler%20project/real_scheduler.cpp), adjust `boost_interval_ms` (default 2000 ms).
