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

