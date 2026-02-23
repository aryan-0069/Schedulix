# OS Scheduler — Complete Concepts & Algorithm Guide

> **Purpose**: This document explains every concept, algorithm, data structure, and design decision in the OS Scheduler project from first principles to complete implementation detail.  After reading this you should be able to re-implement any component from scratch, answer any interview question about it, and explain why each design choice was made.

---

## Table of Contents

1. [The Problem: Why Scheduling Exists](#1-the-problem-why-scheduling-exists)
2. [CPU Scheduler Fundamentals](#2-cpu-scheduler-fundamentals)
3. [Key Metrics Defined](#3-key-metrics-defined)
4. [Algorithm 1 — FCFS](#4-algorithm-1--fcfs)
5. [Algorithm 2 — SJF](#5-algorithm-2--sjf)
6. [Algorithm 3 — SRTF](#6-algorithm-3--srtf)
7. [Algorithm 4 — Round Robin](#7-algorithm-4--round-robin)
8. [Algorithm 5 — MLFQ](#8-algorithm-5--mlfq)
9. [Algorithm 6 — CFS (Completely Fair Scheduler)](#9-algorithm-6--cfs-completely-fair-scheduler)
10. [Fairness Theory](#10-fairness-theory)
11. [Real Process Control on Linux](#11-real-process-control-on-linux)
12. [Timer Mechanisms](#12-timer-mechanisms)
13. [The Simulation Engine](#13-the-simulation-engine)
14. [The Real Scheduler Backend](#14-the-real-scheduler-backend)
15. [Validation Methodology](#15-validation-methodology)
16. [Data Structures Deep Dive](#16-data-structures-deep-dive)
17. [Worked Examples](#17-worked-examples)
18. [Performance Analysis & Trade-offs](#18-performance-analysis--trade-offs)
19. [Common Pitfalls & How We Solve Them](#19-common-pitfalls--how-we-solve-them)
20. [Interview Questions & Answers](#20-interview-questions--answers)

---

## 1. The Problem: Why Scheduling Exists

Modern computers run many processes simultaneously, but a typical system has far fewer CPU cores than processes. The **CPU scheduler** is the kernel subsystem that decides:

1. **Which** process runs next (the selection policy)
2. **When** to take the CPU away from the current process (preemption)
3. **How long** each process runs before being preempted (the quantum)

### Why it matters

Without scheduling, the first process to reach the CPU would monopolize it forever. Scheduling enables:

- **Multitasking** — the illusion of many processes running in parallel on a single core
- **Fairness** — every process eventually gets CPU time
- **Responsiveness** — interactive processes get the CPU quickly
- **Throughput** — maximize the number of processes completing per unit time
- **Resource efficiency** — minimize CPU idle time

### Scheduling vs Dispatching

| Term | Meaning |
|---|---|
| **Scheduler** | The decision-making policy: which process goes next? |
| **Dispatcher** | The mechanism: context switch, mode change, jump to chosen process |
| **Scheduling latency** | Time from "process becomes ready" to "process starts running" |
| **Dispatch latency** | Time for the dispatcher to stop one process and start another |

---

## 2. CPU Scheduler Fundamentals

### Process States

```
                    ┌──────────────┐
         fork()     │     New      │
   ──────────────►  └──────┬───────┘
                           │ admitted
                    ┌──────▼───────┐
         preempt    │    Ready     │◄──────────── I/O complete
   ──────────────►  └──────┬───────┘
                           │ scheduled
                    ┌──────▼───────┐
                    │   Running    │───────────► I/O request ──► Blocked
                    └──────┬───────┘
                           │ exit
                    ┌──────▼───────┐
                    │  Terminated  │
                    └──────────────┘
```

This project focuses on the **Ready → Running → Ready** cycle (the CPU burst phase).

### Preemptive vs Non-Preemptive

| Type | Description | Examples |
|---|---|---|
| **Non-preemptive** | Once a process starts running, it keeps the CPU until it blocks or exits | FCFS, SJF |
| **Preemptive** | The OS can forcibly remove a running process | SRTF, RR, MLFQ, CFS |

Preemptive scheduling requires hardware support (timer interrupts) and increases context-switch overhead.

### Context Switch

A context switch saves the current process's registers, program counter, and memory mappings, then restores the next process's state. It has a real cost (typically 1–100 µs on modern hardware). This is why:
- Small time quanta → more switches → more overhead
- Large time quanta → fewer switches → worse response time

### Scheduler Invocation Points

The scheduler is invoked (called by the kernel) in these situations:
1. A process transitions from Running → Blocked (I/O request)
2. A process transitions from Running → Terminated (exit)
3. A timer interrupt fires (end of quantum — preemptive only)
4. A new process is created or unblocked (potentially preempts current)

---

## 3. Key Metrics Defined

### Time metrics (per process)

```
Timeline:  ══════════════════════════════════════════════════►
              │           │              │             │
           Arrival     First run      Last run      Finish
              │              │
              └── RT ────────┘ (Response Time)
              │                             │
              └─────────── TAT ─────────────┘ (Turnaround Time)
              │              ╠════════╣         (CPU Burst time)
              └──── WT ──────┘                  (Waiting Time)
```

| Symbol | Formula | Definition |
|---|---|---|
| AT | given | Arrival Time — when process enters the ready queue |
| BT | given | Burst Time — total CPU time needed |
| ST | measured | Start Time — first time process gets CPU |
| CT | measured | Completion Time — process exits |
| TAT | CT − AT | Turnaround Time |
| WT | TAT − BT | Waiting Time |
| RT | ST − AT | Response Time (first impression of system responsiveness) |

### System-wide metrics

| Metric | Formula | Goal |
|---|---|---|
| Avg TAT | Σ TATᵢ / n | Minimize |
| Avg WT | Σ WTᵢ / n | Minimize |
| Avg RT | Σ RTᵢ / n | Minimize (critical for interactive systems) |
| CPU Utilization | CPU_busy / Makespan × 100% | Maximize (typically 40–90%) |
| Throughput | Processes / Time unit | Maximize |
| Context Switches | Count | Minimize (overhead) |
| Jain's Fairness J | See §10 | Maximize (1.0 = perfect) |

### The fundamental trade-offs

1. **Throughput vs Response Time**: Minimizing avg TAT (SRTF) often hurts individual response times for long jobs
2. **Fairness vs Efficiency**: Pure fairness (CFS) may have worse avg TAT than SJF which favors short jobs
3. **Overhead vs Quantum size**: Smaller quanta improve RT but increase context-switch overhead
4. **Predictability vs Optimality**: SRTF is optimal but requires burst time knowledge; practical systems use approximations

---

## 4. Algorithm 1 — FCFS

### Definition
**First Come First Served** — processes are served in the order they arrive. Non-preemptive.

### Implementation

```
Sort processes by arrival time.
For each process p in sorted order:
    start = max(current_time, p.arrival)
    current_time = start + p.burst
    Record segment [start, current_time]
```

### Complexity
- Time: O(n log n) — dominated by sort
- Space: O(n)

### Properties

| Property | Value |
|---|---|
| Preemptive | No |
| Optimal | No — average WT can be very high |
| Starvation | No — every process eventually runs |
| Convoy Effect | Yes — one long process can make all others wait |

### Convoy Effect Example

```
P0: AT=0, BT=100   P1: AT=0, BT=1   P2: AT=0, BT=1

FCFS order: [P0: 0-100] [P1: 100-101] [P2: 101-102]

Avg WT = (0 + 100 + 101) / 3 = 67  ← terrible!

With SJF:  [P1: 0-1] [P2: 1-2] [P0: 2-102]
Avg WT = (0 + 0 + 2) / 3 = 0.67  ← much better
```

### When to use
- Batch systems where all jobs have similar lengths
- Simple embedded systems where fairness and predictability matter more than average WT

---

## 5. Algorithm 2 — SJF

### Definition
**Shortest Job First** — among all ready processes, pick the one with the smallest remaining burst time. Non-preemptive: once started, a process runs to completion.

### Implementation

```
Use a min-heap ordered by (burst, arrival, id) for tie-breaking.
current_time = first_arrival

Loop until all done:
    Admit all processes with arrival <= current_time to heap
    If heap empty: jump to next arrival
    Pop (bt, at, id) from heap
    Run [current_time, current_time + bt]
    current_time += bt
```

### Why it minimizes average WT (proof sketch)

Suppose we have two processes with burst times b₁ < b₂. If we run b₂ first:
- Total WT = 0 + b₂ = b₂

If we run b₁ first:
- Total WT = 0 + b₁ = b₁

Since b₁ < b₂, running shorter jobs first reduces total waiting time. This generalises by induction to n processes — SJF is **optimal for non-preemptive average WT**.

### The Oracle Problem

SJF requires knowing burst times in advance. Real systems don't have this.

**Exponential averaging** approximates next burst:
```
τ_(n+1) = α · t_n + (1 − α) · τ_n
```
where `t_n` = actual nth burst, `τ_n` = predicted nth burst, `α ∈ (0,1)`.

With α=0.5, recent bursts are weighted more. This is what real Linux batch schedulers use.

---

## 6. Algorithm 3 — SRTF

### Definition
**Shortest Remaining Time First** — preemptive version of SJF. At any arrival, if the new process's burst is shorter than the remaining time of the running process, preempt.

### Implementation

```
Use a min-heap ordered by (remaining_time, id).
current_time = first_arrival

Loop until all done:
    Admit all arrived processes to heap
    If heap empty: jump to next arrival
    next_arrival = next unadmitted process arrival (or INF)
    Pop (remaining, id)
    run = min(remaining, next_arrival − current_time)
    Execute [current_time, current_time + run]
    current_time += run
    Update remaining[id] -= run
    If remaining > 0: push back to heap
```

### Why SRTF is globally optimal

SRTF is the **preemptive optimal** algorithm for minimizing average waiting time. Proof: at every moment, running the job with least remaining time is the greedy choice that minimizes future waiting. Any deviation would increase total waiting.

### Trade-off

SRTF has the highest context-switch overhead (potentially O(n²) switches in the worst case). The more arrivals and preemptions, the more overhead.

---

## 7. Algorithm 4 — Round Robin

### Definition
**Round Robin** — processes are served in FIFO order from a circular queue; each process runs for at most `quantum` time units before being preempted and placed at the back.

### Implementation

```
Sort by arrival. Initialize ready queue with first arrivals.
Loop until ready queue empty:
    i = dequeue front
    run = min(quantum, remaining[i])
    Execute [current_time, current_time + run]
    current_time += run
    remaining[i] -= run
    Admit all processes with arrival <= current_time
    If remaining[i] > 0: enqueue i at back
    If queue empty and more to arrive: advance time
```

### Critical implementation detail: admission timing

Newly arrived processes should be admitted **after** the currently running slice completes but **before** the preempted process re-enters the queue. This matches hardware behavior — the arriving process is ready before the preempted one gets re-queued.

```
Wrong order:
  re-enqueue preempted process → admit new arrivals
  (new arrivals go after the preempted process)

Correct order:
  admit new arrivals → re-enqueue preempted process
  (new arrivals go before the preempted process if they arrived during the slice)
```

### Quantum choice

Let `q` = quantum, `n` = number of processes, `σ` = context switch overhead:

- **Response time** ≤ (n−1)·q + σ  (bounded!)
- **Turnaround** ≈ burst + (burst/q)·(context switches)
- Rule of thumb: q should be larger than 80% of CPU bursts (i.e., most jobs finish in one quantum)

Modern Linux uses a default quantum of ~4 ms for interactive processes.

---

## 8. Algorithm 5 — MLFQ

### Motivation

Neither RR nor SJF is ideal:
- RR treats all processes equally (ignores actual behavior)
- SJF requires advance knowledge of burst time

MLFQ learns about processes over time: interactive/short processes naturally stay at high priority; long CPU-bound processes get demoted.

### Structure

```
Queue 0: highest priority, smallest quantum Q₀
Queue 1: medium priority,  quantum Q₁ = 2·Q₀
Queue 2: lowest priority,  FCFS (infinite quantum)
...
Queue k: lowest priority
```

### The Five Rules (Ousterhout)

**Rule 1**: If Priority(A) > Priority(B) → A runs.

**Rule 2**: If Priority(A) = Priority(B) → A and B run in Round Robin.

**Rule 3**: When a job enters the system, it starts at the highest priority (top queue).

**Rule 4**: Once a job uses up its time allotment at a given level (regardless of how many times it has given up the CPU), its priority is reduced (moved down one queue).
- **Old Rule 4a** (naive): If a job gives up the CPU voluntarily, stay at same priority.
- **Old Rule 4b** (naive): If a job uses entire quantum, demote.
- **Problem**: A process could game the scheduler by yielding just before quantum expires!
- **New Rule 4** (fixed): Track *total* CPU time at the current level; demote when total allotment exhausted.

**Rule 5 (Priority Boost)**: After every period S, move all jobs to the topmost queue.
- Prevents starvation: long jobs that have been demoted get a fresh start
- Prevents priority inversion: a CPU-bound job that suddenly becomes interactive gets promoted

### Allotment vs Quantum

- **Quantum**: Time per individual slice at a level
- **Allotment**: Total time budget at a level before demotion

A job can have multiple slices at a level if it voluntarily yields; it demotes only when the allotment is exhausted.

### Worked Example

```
Q=5 ticks, Levels: L0(allot=5), L1(allot=10), L2(FCFS)
Boost every 50 ticks.

Process A: AT=0, BT=50 (long CPU-bound)
Process B: AT=15, BT=5  (short interactive)

t=0:   A enters L0
t=0-5: A runs at L0 (allotment exhausted) → demotes to L1
t=5-15: A runs at L1 (uses 10ms allotment) → demotes to L2
t=15:  B enters L0 (higher priority than A at L2)
t=15-20: B runs at L0 (completes in 5ms, stays at L0)
t=20:  A resumes at L2 (FCFS)
t=20-55: A completes

B: TAT=5, WT=0 ← interactive process barely waited!
A: TAT=55, WT=5 ← long process waited for B
```

### Anti-gaming

Without "gaming prevention", a malicious process could do:
```
while running:
    do 99% of quantum worth of work
    yield    ← stays at L0!
    // never demoted despite being CPU-bound
```

The allotment fix: track cumulative CPU time at the level. The above example burns through the L0 allotment in one slice — same demotion speed.

---

## 9. Algorithm 6 — CFS (Completely Fair Scheduler)

### Philosophy

CFS (introduced in Linux 2.6.23, 2007) abandons fixed quanta entirely. Instead, it tracks a **virtual runtime** for each process and always schedules the process that is most "behind" in virtual time.

The goal: every process gets an **equal share of CPU time** (adjusted by weight).

### Virtual Runtime (vruntime)

```
vruntime_delta = actual_time × WEIGHT_NICE0 / weight_i
```

Where:
- `WEIGHT_NICE0 = 1024` is the default weight at nice=0
- `weight_i` is the weight of process i from the nice table
- Heavier processes (lower nice, higher weight) accumulate vruntime **more slowly** → they run longer per unit of real time

### Nice-to-Weight Table

Linux uses a hand-tuned table where each step in nice value is a ~25% weight change:

```
nice  weight   ratio to nice-1
-20   88761    ~1.25x
-15   29154
-10   9548
-5    3121
 0    1024     (baseline)
+5    335
+10   110
+15   36
+19   15
```

This means a process with nice=-5 gets ~3× more CPU than nice=+5. The 25% ratio was chosen so the difference between adjacent levels is perceptible but not overwhelming.

### The Runqueue

In Linux CFS, the runqueue is a **red-black tree** (self-balancing BST) keyed by vruntime. The leftmost node (minimum vruntime) is always cached for O(1) next-process selection.

In our implementation, we use `std::set<RQEntry>` which provides the same O(log n) insert/remove and O(1) min-element access.

### Scheduling Period (target_latency)

The **scheduling period** (target_latency) is the time over which every runnable process gets at least one slice. Linux uses:

```
sched_latency = max(6ms × number_of_processes, 48ms)
```

For our implementation: `CFS_TARGET_LATENCY = 20ms`.

### Slice Calculation

```
slice_i = target_latency × weight_i / total_weight
```

This ensures processes with higher weight get proportionally larger slices.

**Minimum granularity** (default 1ms in our impl, 0.75ms in Linux) prevents pathologically tiny slices with many processes.

### Wakeup and Lag Compensation

When a process sleeps (I/O wait) and then wakes up, its vruntime is far behind the current minimum (min_vruntime). A naïve implementation would give it unlimited CPU time to "catch up."

Linux solves this with **lag compensation**:

```
vruntime_on_wakeup = max(vruntime_of_process,
                         min_vruntime − target_latency)
```

This means a sleeping process wakes up to "one scheduling period behind" min_vruntime — it gets priority but not unlimited catchup.

### min_vruntime

`min_vruntime` is a monotonically increasing floor of all vruntimes in the runqueue. It:
1. Sets the baseline for new/waking processes
2. Prevents vruntime from wrapping (for long-running systems)

```
On each schedule tick:
    min_vruntime = max(min_vruntime, leftmost_node.vruntime)
```

### CFS vs MLFQ comparison

| Aspect | MLFQ | CFS |
|---|---|---|
| Priority model | Multi-level discrete | Continuous weight-based |
| Time tracking | Quantum/allotment | Exact vruntime |
| Starvation | Boost prevents it | min_vruntime lag prevents it |
| Interactive responsiveness | High (stays at L0) | High (low vruntime on wakeup) |
| Configurability | Many knobs (levels, boosts) | nice value only |
| Gaming resistance | Allotment-based (fixed) | Mathematically ungameable |

---

## 10. Fairness Theory

### What is "fair" scheduling?

**Max-min fairness**: Maximize the minimum allocation; then among all allocations that achieve that minimum, maximize the second-minimum, and so on.

**Proportional fairness**: Allocate CPU proportionally to weights. If process i has weight wᵢ and total weight W, process i should receive wᵢ/W fraction of CPU.

### Jain's Fairness Index

Defined by Rajendra Jain (1984):

```
J(x₁, x₂, ..., xₙ) = (Σxᵢ)² / (n · Σxᵢ²)
```

Where `xᵢ` is the "allocation" of resource to user i.

Properties:
- **Range**: 1/n ≤ J ≤ 1
- **J = 1**: Perfect fairness (all allocations equal)
- **J = 1/n**: Worst case (one user gets everything)
- **Independence of scale**: Doubling all allocations doesn't change J
- **Population monotonicity**: Adding another user with equal allocation keeps J ≥ old J

In our implementation:
```
xᵢ = burst_i / turnaround_i
```
This measures what fraction of each task's lifetime was "useful" (on CPU). A perfectly fair scheduler where every task spends the same fraction of time on-CPU gives J=1.

### Normalized Turnaround Time

Another fairness measure:

```
NTT_i = TAT_i / BT_i
```

NTT = 1 means the process's turnaround equals its burst (no waiting — impossible in multi-process systems). A fair scheduler minimizes the maximum NTT.

---

## 11. Real Process Control on Linux

### Fork and Exec

```c
pid_t pid = fork();
if (pid == 0) {
    // Child process: exact copy of parent
    exec(...)  // replace with new program, or just run work directly
} else {
    // Parent process: pid = child's PID
}
```

In our project, children don't exec — they run the workload function directly.

### SIGSTOP and SIGCONT

| Signal | Effect |
|---|---|
| `SIGSTOP` | Forces process into stopped state; **cannot be caught, blocked, or ignored** |
| `SIGCONT` | Resumes a stopped process; also cannot be blocked |
| `SIGSTP` | Like SIGSTOP but can be caught (used by Ctrl+Z in terminals) |

Our scheduler uses SIGSTOP/SIGCONT to simulate preemption:

```
1. Child forks and immediately raises(SIGSTOP) — parks itself
2. Parent confirms stop via waitpid(WUNTRACED)
3. Scheduler sends SIGCONT to grant CPU
4. SIGALRM fires at quantum boundary → parent sends SIGSTOP
5. Parent waits for confirmed stop → picks next task
```

**Critical**: Always confirm the stop before sending SIGCONT to the next process. Without this, two processes can run simultaneously.

### waitpid Flags

| Flag | Meaning |
|---|---|
| `WUNTRACED` | Also return if child has been stopped (not just exited) |
| `WCONTINUED` | Also return if child has been resumed |
| `WNOHANG` | Return immediately if no child has changed state |

```c
// Block until pid is confirmed stopped
int status;
waitpid(pid, &status, WUNTRACED);
if (WIFSTOPPED(status)) { /* confirmed stopped */ }
if (WIFEXITED(status))  { /* confirmed exited */ }
```

### CLOCK_PROCESS_CPUTIME_ID

This clock type measures **CPU time consumed by the process** — it does NOT advance while the process is stopped. This is exactly what we want for measuring burst progress:

```c
struct timespec start, cur;
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
// ... busy work ...
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cur);
double cpu_ms = (cur.tv_sec - start.tv_sec) * 1000.0
              + (cur.tv_nsec - start.tv_nsec) / 1e6;
```

If the process is SIGSTOP'd during this loop, the clock pauses. Resume with SIGCONT and the clock continues. The total measured CPU time is exactly the burst budget.

### Process Groups and Signal Safety

When a child forks, it inherits the parent's signal handlers. We must be careful:
- Children should not receive SIGALRM meant for the parent
- Use `setpgrp()` or `setsid()` to put children in their own process group for clean signal isolation

In our implementation, we send SIGSTOP/SIGCONT to individual PIDs (not groups) to avoid this issue.

---

## 12. Timer Mechanisms

### setitimer / SIGALRM (our approach)

```c
struct itimerval itv{};
itv.it_value.tv_sec  = 0;
itv.it_value.tv_usec = quantum_us;  // quantum in microseconds
setitimer(ITIMER_REAL, &itv, nullptr);
// SIGALRM fires after quantum_us microseconds
```

**Resolution**: ~1 ms on modern Linux (hardware timer tick granularity).

**Limitation**: Only one SIGALRM timer per process. Cannot use it for multiple concurrent timers.

**Why `ITIMER_REAL`?** It counts wall-clock time (not CPU time), so it fires even when the process is blocked. This is what we want — the parent needs to preempt the child after wall-clock time, not parent's CPU time.

### timerfd (Linux alternative)

`timerfd` provides a file-descriptor-based timer that can be read with `read()` or polled with `epoll()`:

```c
int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
struct itimerspec ts{};
ts.it_value.tv_nsec = quantum_ns;
timerfd_settime(tfd, 0, &ts, nullptr);
// Block until timer fires:
uint64_t exp; read(tfd, &exp, sizeof(exp));
```

Advantages:
- Integrable with event loops (epoll/select)
- Multiple timers possible (one fd per timer)
- No signal-based asynchrony — cleaner programming model

Our implementation uses `setitimer` for simplicity; `timerfd` would be preferable in a production system.

### Signal delivery latency

Even with a perfectly set timer, SIGALRM is delivered asynchronously. The kernel queues the signal and delivers it at the next scheduler opportunity. Typical delivery latency: 10–100 µs. This means our slices may overshoot by that amount — measurable in our validation reports.

---

## 13. The Simulation Engine

### Event-driven simulation

The simulator runs in **logical time** (ticks), not wall-clock time. It processes events in chronological order:

```
Events:
- Process arrival (AT): process enters ready queue
- Quantum expiry: currently running process is preempted
- Process completion: process exits

Event loop:
  1. At current_time, admit all newly arrived processes
  2. Pick the next process according to the algorithm's policy
  3. Determine how long it can run: min(burst_remaining, next_event_time - current_time)
  4. Record segment [current_time, current_time + run]
  5. Advance current_time += run
  6. Update state
  7. Goto 1
```

### Why build a simulator?

1. **Instant results**: O(n log n) vs real-time execution
2. **Determinism**: No OS jitter; perfect repeatability
3. **Validation baseline**: Compare against real backend to detect bugs
4. **Exploration**: Test many configurations quickly

### Simulator accuracy limitations

- Ignores context-switch overhead (assumed zero)
- Assumes process executes exactly `burst` ticks (no variance)
- Treats time as discrete integers (rounding errors at boundaries)
- Does not model I/O, memory pressure, cache effects

---

## 14. The Real Scheduler Backend

### Architecture

```
Parent process (scheduler):
  1. Fork n children, each immediately parks with raise(SIGSTOP)
  2. Wait for all children to confirm stopped (waitpid WUNTRACED)
  3. Run scheduling loop:
     a. Select next process
     b. Arm SIGALRM timer with slice duration
     c. Send SIGCONT to chosen child
     d. Poll: wait for SIGALRM or child exit
     e. On SIGALRM: send SIGSTOP to child, wait for confirmation
     f. Record [start_time, end_time] segment
     g. Goto 3a
```

### Why poll instead of blocking wait?

We need to detect two events: timer expiry (SIGALRM) and child exit. We can't use `waitpid()` without WNOHANG because it would block on exit but not see SIGALRM.

Solution: poll with short sleep + check SIGALRM flag:

```c
volatile sig_atomic_t g_alarm_fired = 0;
void sigalrm_handler(int) { g_alarm_fired = 1; }

// Poll loop in parent:
while (!g_alarm_fired) {
    pid_t r = waitpid(child_pid, &status, WNOHANG | WUNTRACED);
    if (r == child_pid && (WIFEXITED(status) || WIFSIGNALED(status))) {
        exited = true; break;
    }
    nanosleep({0, 100000}, nullptr);  // 0.1ms sleep
}
```

The 0.1ms polling interval adds at most 0.1ms measurement error — acceptable for bursts ≥ 50ms.

### CPU time measurement per child

The child uses `CLOCK_PROCESS_CPUTIME_ID` internally to count its own CPU time.  The parent cannot directly query this clock for another process (without reading `/proc/PID/stat`).

In our design, the child is responsible for stopping itself when its burst is complete:

```c
// In child:
clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
while (elapsed_cpu_time < burst_ms) {
    // busy work
}
_exit(0);
```

The parent sees the child exit and records the wall-clock completion time. The child's CPU clock ensures it always burns exactly `burst_ms` of CPU, regardless of how many times it was paused.

### Signal Safety

Rules for signal handlers:
- Only async-signal-safe functions are allowed (write, _exit, but NOT malloc, printf)
- We use volatile `sig_atomic_t` for flags shared between signal handler and main code
- No mutexes inside signal handlers

---

## 15. Validation Methodology

### What we're testing

The simulator is a **model**. The real backend is **reality**. Validation measures how accurate the model is.

### Matching workloads

Both simulator and real backend receive identical:
- Process arrival times
- Burst sizes (ticks = ms)
- Nice values
- Scheduling algorithm
- Quantum

### Metrics compared

For each algorithm, we compute:

```
absolute_error(metric) = |simulated_value - real_value|
percent_error(metric)  = |simulated_value - real_value| / simulated_value × 100%
```

### Expected error ranges

| Burst size | Expected TAT error | Cause |
|---|---|---|
| 10 ms | 20-50% | Signal latency dominates |
| 50 ms | 5-15% | Acceptable for demonstrations |
| 200 ms | 1-5% | Good match |
| 1000 ms | < 1% | Excellent match |

### Sources of error

1. **Signal delivery latency**: SIGALRM arrives ~10-100µs late → slice overshoots
2. **SIGSTOP/SIGCONT overhead**: Each preemption costs ~50µs for signal + kernel state save/restore
3. **OS scheduling jitter**: The kernel's own scheduler decides when the parent runs after SIGALRM
4. **Timer resolution**: setitimer has ~1ms resolution; short quanta have higher relative error
5. **Fork overhead**: First slice slightly longer due to process initialization

### Validation report interpretation

```
Avg TAT error < 5%  → simulator is a good predictor
Avg TAT error 5-15% → acceptable with noted limitations
Avg TAT error > 15% → investigate — likely a bug or wrong burst size
Context switch delta = 0 → algorithm implemented correctly
Jain's J error < 0.05 → fairness model accurate
```

---

## 16. Data Structures Deep Dive

### Task (input)

```cpp
struct Task {
    int id;        // 0-based index
    int arrival;   // when process enters ready queue
    int burst;     // total CPU work needed
    int nice;      // [-20, 19]; maps to weight via niceToWeight()
    int priority;  // for MLFQ static initial placement
};
```

### Segment (timeline unit)

```cpp
struct Segment {
    int    id;      // which process
    double start;   // inclusive (supports fractional ms in real backend)
    double end;     // exclusive
    int    level;   // MLFQ level (0 = highest)
};
```

The complete `vector<Segment>` is the **ground truth** from which all metrics are derived. This is the same structure regardless of algorithm or backend.

### TaskResult (derived metrics)

Computed by `buildResults(tasks, timeline)`:
```cpp
struct TaskResult {
    int    id;
    double start_time;
    double completion_time;
    double turnaround;
    double waiting;
    double response;
    int    context_switches;
};
```

### RunSummary (aggregate)

```cpp
struct RunSummary {
    std::string name;
    double avg_turnaround;
    double avg_waiting;
    double avg_response;
    double cpu_utilization;
    int    context_switches;
    double jains_fairness;
    double makespan;
};
```

### Priority Queue for SJF/SRTF

```cpp
// Min-heap by (burst, arrival, id) for SJF
using T3 = std::tuple<int,int,int>;
std::priority_queue<T3, std::vector<T3>, std::greater<T3>> rq;
// Push: rq.push({burst, arrival, id})
// Pop:  auto [bt, at, id] = rq.top(); rq.pop();
```

The 3-tuple tie-breaking ensures deterministic behavior when multiple processes have equal burst times.

### std::set for CFS runqueue

```cpp
struct RQEntry {
    double vruntime;
    int    id;
    bool operator<(const RQEntry& o) const {
        if (vruntime != o.vruntime) return vruntime < o.vruntime;
        return id < o.id;  // stable tie-breaking
    }
};
std::set<RQEntry> rq;
// Min vruntime: rq.begin()   O(1) — set maintains order
// Insert:       rq.insert()  O(log n)
// Remove:       rq.erase()   O(log n)
```

This mirrors the Linux kernel's red-black tree runqueue.

### MLFQ queue array

```cpp
std::vector<std::deque<int>> queues(num_levels);
// queues[0] = highest priority deque of process indices
// queues[k] = lowest priority
```

Each deque supports O(1) front access and O(1) push_front/push_back for reinsertion.

---

## 17. Worked Examples

### FCFS Example (verified)

```
Input:
  P0: AT=0, BT=4
  P1: AT=1, BT=3
  P2: AT=2, BT=5

Execution:
  t=0: P0 arrives, is alone → P0 starts
  t=4: P0 finishes. P1 (arrived t=1), P2 (arrived t=2) both ready.
       FCFS: pick P1 (arrived first among waiting)
  t=7: P1 finishes. P2 ready.
  t=12: P2 finishes.

Results:
  P0: ST=0, CT=4,  TAT=4-0=4,  WT=4-4=0
  P1: ST=4, CT=7,  TAT=7-1=6,  WT=6-3=3
  P2: ST=7, CT=12, TAT=12-2=10, WT=10-5=5

  Avg TAT = (4+6+10)/3 = 6.67
  Avg WT  = (0+3+5)/3  = 2.67

Gantt: +--------+------+----------+
       |   P0   |  P1  |    P2    |
       +--------+------+----------+
       0        4      7          12
```

### SRTF Example (verified)

```
Input:
  P0: AT=0, BT=8
  P1: AT=1, BT=4
  P2: AT=2, BT=9
  P3: AT=3, BT=5

Step-by-step:
  t=0: Only P0 ready (rem=8). P0 runs.
  t=1: P1 arrives (rem=4). rem(P0)=7 > rem(P1)=4. Preempt P0!
       P1 runs.
  t=2: P2 arrives (rem=9). rem(P2)=9 > rem(P1)=3. P1 continues.
  t=3: P3 arrives (rem=5). rem(P3)=5 > rem(P1)=2. P1 continues.
  t=5: P1 finishes (rem=0). Ready: P0(7), P2(9), P3(5).
       Pick P3 (rem=5, smallest).
  t=10: P3 finishes. Ready: P0(7), P2(9). Pick P0.
  t=17: P0 finishes. Only P2 (rem=9). P2 runs.
  t=26: P2 finishes.

Results:
  P0: ST=0,  CT=17, TAT=17, WT=17-8=9
  P1: ST=1,  CT=5,  TAT=4,  WT=4-4=0
  P2: ST=17, CT=26, TAT=24, WT=24-9=15
  P3: ST=5,  CT=10, TAT=7,  WT=7-5=2

  Avg TAT = (17+4+24+7)/4 = 13.0
  Avg WT  = (9+0+15+2)/4  = 6.5

Gantt: +--+--------+----------+--------------+------------------+
       |P0|   P1   |    P3    |      P0      |        P2        |
       +--+--------+----------+--------------+------------------+
       0  1        5          10             17                 26
```

### Round Robin Example (q=2, verified)

```
Input: P0(BT=5), P1(BT=3), P2(BT=1), P3(BT=2), all AT=0

Initial queue: [P0, P1, P2, P3]

t=0:  Run P0 [0,2], rem=3. Admit none. Queue: [P1, P2, P3, P0]
t=2:  Run P1 [2,4], rem=1. Queue: [P2, P3, P0, P1]
t=4:  Run P2 [4,5], rem=0. P2 done! Queue: [P3, P0, P1]
t=5:  Run P3 [5,7], rem=0. P3 done! Queue: [P0, P1]
t=7:  Run P0 [7,9], rem=1. Queue: [P1, P0]
t=9:  Run P1 [9,10], rem=0. P1 done! Queue: [P0]
t=10: Run P0 [10,11], rem=0. P0 done!

Results:
  P0: ST=0,  CT=11, TAT=11, WT=6
  P1: ST=2,  CT=10, TAT=10, WT=7
  P2: ST=4,  CT=5,  TAT=5,  WT=4
  P3: ST=5,  CT=7,  TAT=7,  WT=5

  Avg TAT = (11+10+5+7)/4 = 8.25
  Avg WT  = (6+7+4+5)/4   = 5.5

Context switches: P0→P1→P2→P3→P0→P1→P0 = 6 switches
```

### CFS vruntime trace (2 processes, equal weight)

```
P0: AT=0, BT=10, weight=1024 (nice=0)
P1: AT=0, BT=10, weight=1024 (nice=0)
target_latency=10, min_gran=1

Initial: P0.vrt=0, P1.vrt=0, total_weight=2048
Slice = (10 × 1024) / 2048 = 5

t=0:  Pick P0 (vrt=0, tied — break by id). Run [0,5].
      P0.vrt += 5 × 1024/1024 = 5.  P0.vrt=5.
t=5:  Pick P1 (vrt=0 < P0.vrt=5). Run [5,10].
      P1.vrt += 5. P1.vrt=5.
t=10: Pick P0 (tied again). Run [10,15]. P0.vrt=10.
t=15: Pick P1. Run [15,20]. P1.vrt=10.
... and so on, perfectly alternating.

Result: Both complete at t=20.
P0: TAT=20, WT=10. P1: TAT=20, WT=10.
Jain's J = 1.0 (perfect fairness)
```

---

## 18. Performance Analysis & Trade-offs

### Time Complexity

| Algorithm | Time | Space |
|---|---|---|
| FCFS | O(n log n) | O(n) |
| SJF | O(n log n) | O(n) |
| SRTF | O(n log n) | O(n) — up to O(n²) segments |
| Round Robin | O(n log n + S) — S=segments | O(n) |
| MLFQ | O(S log n) — S=segments | O(n) |
| CFS | O(S log n) — S=segments | O(n) |

### Average Metrics Comparison (typical workloads)

| Algorithm | Avg TAT | Avg WT | Avg RT | Fairness | Ctx Sw |
|---|---|---|---|---|---|
| FCFS | High | High | Low | Poor | Minimal |
| SJF | Low | Low | Moderate | Medium | Minimal |
| SRTF | Lowest | Lowest | Moderate | Medium | High |
| RR | Medium | Medium | Best | Good | High |
| MLFQ | Low-Medium | Low-Medium | Best | Good | Medium |
| CFS | Medium | Medium | Good | Best | Medium |

### The Exploitation vs Exploration Trade-off

- **SRTF** exploits known burst times to be optimal — but these are rarely known
- **MLFQ** explores (treats new processes as short, learns from behavior)
- **CFS** explores through continuous vruntime adjustment

### Cache Efficiency

Real schedulers also consider cache locality. A process that was recently running may have hot cache lines. Re-scheduling it quickly after preemption is cache-friendly. This is why Linux CFS has the **buddy system** — after a wakeup, it may preempt the current task in favor of its "buddy" (the task that just woke it).

---

## 19. Common Pitfalls & How We Solve Them

### Pitfall 1: Double output after fork()

**Problem**: After `fork()`, both parent and child have buffered stdio data. If the parent calls `printf()` before forking, both will flush those buffers on exit — double output.

**Solution**: Children use `_exit(0)` not `exit(0)`. `_exit` immediately terminates without flushing stdio buffers.

### Pitfall 2: SIGCONT before confirming SIGSTOP

**Problem**: After sending SIGSTOP to a child, there's a window where the child is still running (SIGSTOP is delivered asynchronously). If the parent immediately sends SIGCONT to the next child, two children run simultaneously.

**Solution**: Always call `waitpid(pid, &status, WUNTRACED)` and check `WIFSTOPPED(status)` before considering a child stopped.

### Pitfall 3: SIGALRM race with child exit

**Problem**: The child might exit exactly as SIGALRM fires. The parent then tries to send SIGSTOP to an already-exited process.

**Solution**: `kill()` with a dead PID returns `ESRCH` (no such process). We check for `ESRCH` and handle it gracefully in `xkill()`.

### Pitfall 4: SRTF wrong segment split

**Problem**: When a new process arrives and preempts the current process, the segment must be split at the arrival time, not at the preemption response time.

**Solution**: `run = min(remaining, next_arrival - current_time)`. This ensures the segment ends exactly when the new process arrives.

### Pitfall 5: RR admission order

**Problem**: Admitting new processes after re-queuing the preempted process gives wrong results (new process goes behind preempted one when it should go in front in some implementations).

**Solution**: Admit new arrivals before deciding whether to re-queue the preempted process. Our implementation admits during the slice, then re-queues.

### Pitfall 6: CFS vruntime precision

**Problem**: Floating-point accumulation errors in vruntime after thousands of iterations can cause incorrect scheduling order.

**Solution**: Use `double` (64-bit) for vruntime. The error is ~1 part in 10¹⁵ — negligible for practical burst counts.

### Pitfall 7: min_vruntime not monotonic

**Problem**: If we set `min_vruntime = rq.begin()->vruntime` and the runqueue is temporarily empty, then fill with a new arrival at vruntime=0, min_vruntime goes backwards.

**Solution**: `min_vruntime = max(min_vruntime, computed_value)` — it only moves forward.

---

## 20. Interview Questions & Answers

**Q: What is the difference between preemptive and non-preemptive scheduling?**

A: In non-preemptive scheduling (FCFS, SJF), once a process gets the CPU it keeps it until it voluntarily yields (I/O, exit). In preemptive scheduling (SRTF, RR, MLFQ, CFS), the OS can forcibly remove a running process via a timer interrupt. Preemptive scheduling improves response time for interactive processes but incurs context-switch overhead.

**Q: Why is SRTF optimal for average waiting time?**

A: Greedy argument: at any moment, running the process with the shortest remaining time minimizes the waiting time accumulated by other processes in the ready queue. If we ran a longer process instead, shorter processes would wait unnecessarily. By induction over scheduling decisions, SRTF minimizes total (and hence average) waiting time.

**Q: What is the convoy effect in FCFS?**

A: One long CPU-bound process can monopolize the CPU, causing all shorter processes behind it to wait — like a convoy behind a slow truck. With FCFS, average waiting time can be very high if short processes arrive after a long one.

**Q: How does MLFQ prevent starvation?**

A: Through periodic priority boosts (Rule 5). Every S ticks, all processes are moved to the top queue (Level 0). Even a CPU-bound process that has been repeatedly demoted to the lowest level gets periodic access to the CPU.

**Q: What is vruntime in CFS?**

A: Virtual runtime is the total CPU time a process has used, weighted by its priority. Specifically: `vruntime_delta = real_time × WEIGHT_NICE0 / weight_i`. Heavier processes (lower nice) accumulate vruntime more slowly, so they run longer per scheduling period. CFS always schedules the process with the smallest vruntime, ensuring proportional CPU share.

**Q: How does the quantum size affect Round Robin performance?**

A: A very small quantum improves response time (every process gets CPU very quickly) but increases overhead because of frequent context switches. A very large quantum degenerates to FCFS (each process runs to completion). The optimal quantum is slightly larger than the typical CPU burst for interactive processes.

**Q: Why use CLOCK_PROCESS_CPUTIME_ID instead of CLOCK_MONOTONIC for child burst measurement?**

A: CLOCK_MONOTONIC measures wall-clock time and keeps advancing even when the process is stopped (via SIGSTOP). CLOCK_PROCESS_CPUTIME_ID only advances while the process is actually scheduled. This is essential for correct burst measurement — a SIGSTOP'd process should not "use up" its burst budget while waiting.

**Q: What is Jain's Fairness Index and what does a value of 0.5 mean?**

A: Jain's Fairness Index J = (Σxᵢ)² / (n·Σxᵢ²) where xᵢ is each user's normalized allocation. J=1.0 means perfect fairness; J=1/n is the worst case. J=0.5 for 2 users means one user is getting most of the resource (equivalent to 1/(n·J) = 1 user getting everything, interpolated).

**Q: How would you improve the real scheduler's accuracy?**

A: Several approaches:
1. Use timerfd instead of SIGALRM for lower-jitter timer delivery
2. Use `CPU_SET` + `sched_setaffinity` to pin parent and children to the same core (no migration jitter)
3. Use `SCHED_FIFO` for the parent process to get real-time priority
4. Use `nanosleep` with remaining-time re-check to account for early wakeup from signals
5. Increase burst sizes to reduce relative measurement error
6. Average multiple runs to smooth OS jitter

**Q: What is the difference between throughput and turnaround time optimization?**

A: Throughput = processes/time_unit — maximize by keeping the CPU busy (high utilization, batch workloads). Turnaround time = time from arrival to completion for individual processes — minimize by running processes quickly. These can conflict: a scheduler that favors short jobs (like SJF) reduces average TAT but may starve long jobs, reducing their individual throughput. CFS balances both by giving everyone a proportional share.
