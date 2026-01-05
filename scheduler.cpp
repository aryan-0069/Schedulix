/**
 * @file    scheduler.cpp
 * @brief   CPU Scheduling Simulator
 *          Supports: FCFS | SJF (non-preemptive) | SRTF (preemptive SJF) | Round Robin
 *          Runs ALL algorithms on the same process set and prints a comparison table.
 *
 * Compile:  g++ -std=c++17 -O2 -Wall -o scheduler scheduler.cpp
 * Run:      ./scheduler
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <queue>
#include <deque>
#include <numeric>
#include <iomanip>
#include <stdexcept>
#include <string>

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr int INF = 1'000'000'000;

// ─── Data Structures ─────────────────────────────────────────────────────────

struct Process {
    int pid;
    int arrival_time;
    int burst_time;
};

struct ProcessResult {
    int pid;
    int arrival_time;
    int burst_time;
    int start_time;       // first time the process gets the CPU
    int completion_time;
    int turnaround_time;  // completion_time - arrival_time
    int waiting_time;     // turnaround_time - burst_time
};

/**
 * One contiguous slice of CPU execution: "process `pid` ran from `start` to `end`."
 * Non-preemptive algorithms (FCFS, SJF) produce one segment per process.
 * Preemptive algorithms (SRTF, RR) produce multiple segments per process.
 * The full ordered list IS the timeline — Gantt chart, utilization, and context
 * switch count are all derived from it.
 */

struct ExecutionSegment {
    int pid;
    int start;
    int end;
};

/**
 * Aggregate metrics for one algorithm, used to build the final comparison table.
 */
struct AlgorithmSummary {
    std::string name;
    double avg_turnaround_time;
    double avg_waiting_time;
    int context_switches;
    double cpu_utilization;
};

// ─── Utilities ────────────────────────────────────────────────────────────────

static void sortByArrival(std::vector<Process>& procs) {
    std::sort(procs.begin(), procs.end(), [](const Process& a, const Process& b) {
        return a.arrival_time < b.arrival_time ||
               (a.arrival_time == b.arrival_time && a.pid < b.pid);
    });
}

