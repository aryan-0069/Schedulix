/**
 * @file   simulator.cpp
 * @brief  Pure-simulation scheduling engine.
 *
 *  Implements FCFS, SJF (non-preemptive), SRTF (preemptive SJF),
 *  Round Robin, MLFQ (Multi-Level Feedback Queue), and CFS
 *  (Completely Fair Scheduler) entirely in software — no real processes.
 *
 *  All algorithms share the same Task/Segment/TaskResult types from common.hpp
 *  so their outputs can be fed directly into the comparison table.
 *
 * Compile (standalone, no real-backend):
 *   g++ -std=c++17 -O2 -Wall -o simulator simulator.cpp
 * Run:
 *   ./simulator
 */

#include "common.hpp"

#include <deque>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

// ============================================================================
//  FCFS — First Come First Served (non-preemptive)
// ============================================================================

std::vector<Segment> fcfs(const std::vector<Task>& tasks) {
    std::vector<Task> procs = tasks;
    std::sort(procs.begin(), procs.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });

    std::vector<Segment> tl;
    double cur = 0;
    for (const auto& p : procs) {
        double start = std::max(cur, static_cast<double>(p.arrival));
        tl.push_back({p.id, start, start + p.burst});
        cur = start + p.burst;
    }
    return tl;
}

// ============================================================================
//  SJF — Shortest Job First (non-preemptive)
// ============================================================================

std::vector<Segment> sjf(const std::vector<Task>& tasks) {
    std::vector<Task> procs = tasks;
    std::sort(procs.begin(), procs.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });

    // min-heap: (burst, arrival, id)
    using T3 = std::tuple<int,int,int>;
    std::priority_queue<T3, std::vector<T3>, std::greater<T3>> ready;

    std::vector<Segment> tl;
    int idx = 0;
    double cur = (procs.empty() ? 0 : procs[0].arrival);

    while (idx < (int)procs.size() || !ready.empty()) {
        while (idx < (int)procs.size() && procs[idx].arrival <= cur) {
            ready.push({procs[idx].burst, procs[idx].arrival, procs[idx].id});
            idx++;
        }
        if (ready.empty()) { cur = procs[idx].arrival; continue; }
        auto [bt, at, id] = ready.top(); ready.pop();
        tl.push_back({id, cur, cur + bt});
        cur += bt;
    }
    return tl;
}

// ============================================================================
//  SRTF — Shortest Remaining Time First (preemptive SJF)
// ============================================================================

std::vector<Segment> srtf(const std::vector<Task>& tasks) {
    std::vector<Task> procs = tasks;
    std::sort(procs.begin(), procs.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival;
    });

    const int n = static_cast<int>(procs.size());
    std::vector<int> rem(n);
    for (int i = 0; i < n; i++) rem[i] = procs[i].burst;

    // min-heap: (remaining, id)
    using P2 = std::pair<int,int>;
    std::priority_queue<P2, std::vector<P2>, std::greater<P2>> ready;

    std::vector<Segment> tl;
    int idx = 0;
    double cur = (n > 0 ? procs[0].arrival : 0);

    auto push_ready = [&](double t) {
        while (idx < n && procs[idx].arrival <= t) {
            ready.push({rem[idx], procs[idx].id});
            idx++;
        }
    };

    while (idx < n || !ready.empty()) {
        push_ready(cur);
        if (ready.empty()) { cur = procs[idx].arrival; continue; }

        double next_arrival = (idx < n) ? procs[idx].arrival : INF_TIME;
        auto [rm, id] = ready.top(); ready.pop();

        // Find index in procs by id
        double run = std::min(static_cast<double>(rm), next_arrival - cur);
        if (run <= 0) run = rm; // no future arrival; run to completion

        tl.push_back({id, cur, cur + run});
        cur += run;
        // update remaining — find this task
        for (int i = 0; i < n; i++) {
            if (procs[i].id == id) { rem[i] -= static_cast<int>(run); break; }
        }
        // If not finished, push back; otherwise mark done
        for (int i = 0; i < n; i++) {
            if (procs[i].id == id && rem[i] > 0) {
                ready.push({rem[i], id});
                break;
            }
        }
    }
    return tl;
}

// ============================================================================
//  Round Robin
// ============================================================================

std::vector<Segment> roundRobin(const std::vector<Task>& tasks, int quantum) {
    if (quantum <= 0) throw std::invalid_argument("quantum must be > 0");
    std::vector<Task> procs = tasks;
    std::sort(procs.begin(), procs.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });

    const int n = static_cast<int>(procs.size());
    std::vector<int> rem(n);
    for (int i = 0; i < n; i++) rem[i] = procs[i].burst;

    std::deque<int> ready;          // indices into procs
    std::vector<bool> in_queue(n, false);
    std::vector<Segment> tl;
    int idx = 0;
    double cur = (n > 0 ? procs[0].arrival : 0);

    // Seed queue with processes that arrive at time 0 (or first arrival)
    while (idx < n && procs[idx].arrival <= cur) {
        ready.push_back(idx);
        in_queue[idx] = true;
        idx++;
    }

    while (!ready.empty()) {
        int i = ready.front(); ready.pop_front(); in_queue[i] = false;

        int run = std::min(quantum, rem[i]);
        tl.push_back({procs[i].id, cur, cur + run});
        cur += run;
        rem[i] -= run;

        // Admit newly arrived processes (arrived during this slice)
        while (idx < n && procs[idx].arrival <= cur) {
            if (!in_queue[idx]) {
                ready.push_back(idx);
                in_queue[idx] = true;
            }
            idx++;
        }

        if (rem[i] > 0) {
            ready.push_back(i);
            in_queue[i] = true;
        }

        // If queue empty and more to come, jump to next arrival
        if (ready.empty() && idx < n) {
            cur = procs[idx].arrival;
            while (idx < n && procs[idx].arrival <= cur) {
                if (!in_queue[idx]) {
                    ready.push_back(idx);
                    in_queue[idx] = true;
                }
                idx++;
            }
        }
    }
    return tl;
}

