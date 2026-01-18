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

