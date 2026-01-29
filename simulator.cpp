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

// ============================================================================
//  MLFQ — Multi-Level Feedback Queue
//
//  Architecture:
//    Level 0: quantum = Q         (highest priority, shortest slice)
//    Level 1: quantum = 2*Q
//    Level 2: FCFS               (lowest priority, runs to completion)
//
//  Rules:
//    R1. New process enters Level 0.
//    R2. If a process uses its full time allotment at level i,
//        it moves down to level i+1.
//    R3. If a process voluntarily yields before the allotment expires,
//        it stays at the same level.
//    R4. Every BOOST_INTERVAL ticks, all processes are moved to Level 0
//        (prevents starvation).
//    R5. When a higher-level process arrives, it preempts the currently
//        running lower-level process.
// ============================================================================

std::vector<Segment> mlfq(const std::vector<Task>& tasks,
                           int base_quantum     = DEFAULT_QUANTUM,
                           int boost_interval   = MLFQ_BOOST_INTERVAL,
                           int levels           = MLFQ_LEVELS)
{
    if (tasks.empty()) return {};
    std::vector<Task> procs = tasks;
    std::sort(procs.begin(), procs.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival;
    });

    const int n = static_cast<int>(procs.size());

    // Per-process state
    struct PState {
        int  remaining;      // CPU time still needed
        int  level;          // current MLFQ queue level
        int  allotment_left; // ticks left in current level's allotment
        bool done;
    };
    std::vector<PState> state(n);
    for (int i = 0; i < n; i++) {
        state[i] = {procs[i].burst, 0, base_quantum, false};
    }

    // quantum[l] = base_quantum * 2^l
    auto qLevel = [&](int l) {
        if (l >= levels - 1) return INF_TIME; // last level = FCFS
        return base_quantum * (1 << l);
    };

    // Ready queues: one deque per level, holding process index
    std::vector<std::deque<int>> queues(levels);

    std::vector<Segment> tl;
    int idx = 0;
    double cur = procs[0].arrival;
    double last_boost = cur;

    // Admit all processes that have arrived
    auto admit = [&](double t) {
        while (idx < n && procs[idx].arrival <= t) {
            state[idx].level = 0;
            state[idx].allotment_left = qLevel(0);
            queues[0].push_back(idx);
            idx++;
        }
    };

    // Find the highest-priority non-empty queue
    auto pick_level = [&]() -> int {
        for (int l = 0; l < levels; l++)
            if (!queues[l].empty()) return l;
        return -1;
    };

    admit(cur);

    while (true) {
        // Priority boost check
        if (cur - last_boost >= boost_interval) {
            last_boost = cur;
            // Move everything to level 0
            for (int l = 1; l < levels; l++) {
                for (int pi : queues[l]) {
                    state[pi].level = 0;
                    state[pi].allotment_left = qLevel(0);
                    queues[0].push_back(pi);
                }
                queues[l].clear();
            }
        }

        int lv = pick_level();
        if (lv == -1) {
            // No ready processes — jump to next arrival if any
            if (idx < n) {
                cur = procs[idx].arrival;
                admit(cur);
                continue;
            }
            break; // all done
        }

        int pi = queues[lv].front();
        queues[lv].pop_front();

        // Maximum we can run: min(allotment_left, remaining, until_next_arrival_or_boost)
        double next_arrival = (idx < n) ? procs[idx].arrival : INF_TIME;
        double next_boost   = last_boost + boost_interval;
        double until_event  = std::min(next_arrival, next_boost) - cur;
        int run = std::min({state[pi].allotment_left,
                            state[pi].remaining,
                            static_cast<int>(std::max(1.0, until_event))});
        if (run <= 0) run = 1;

        tl.push_back({procs[pi].id, cur, cur + run, lv});
        cur += run;
        state[pi].remaining      -= run;
        state[pi].allotment_left -= run;

        // Admit new arrivals
        admit(cur);

        if (state[pi].remaining <= 0) {
            state[pi].done = true;
            // Task finished — do not requeue
        } else if (state[pi].allotment_left <= 0) {
            // Demote to next level
            int next_lv = std::min(lv + 1, levels - 1);
            state[pi].level = next_lv;
            state[pi].allotment_left = qLevel(next_lv);
            queues[next_lv].push_back(pi);
        } else {
            // Used partial allotment (was preempted by higher-level arrival)
            // Check if there's now a higher-priority process
            int new_lv = pick_level();
            if (new_lv < lv || (new_lv == lv && !queues[lv].empty())) {
                // Put ourselves back at the head of our level
                queues[lv].push_front(pi);
            } else {
                // Stay at front of our level
                queues[lv].push_front(pi);
            }
        }
    }
    return tl;
}

// ============================================================================
//  CFS — Completely Fair Scheduler
//
//  Core ideas:
//    - Each task tracks `vruntime` = sum of (real_time * WEIGHT_NICE0 / weight_i)
//    - Scheduler always picks the task with the smallest vruntime (most "behind").
//    - Slice size = (target_latency * weight_i) / total_weight  [clamped to min_gran]
//    - On wakeup, vruntime is set to max(current_vruntime, min_vruntime - target_latency)
//      so sleeping tasks don't hoard time-share.
//    - min_vruntime monotonically increases (the "floor" of the runqueue).
// ============================================================================

std::vector<Segment> cfs(const std::vector<Task>& tasks,
                          double target_latency   = CFS_TARGET_LATENCY,
                          double min_granularity  = CFS_MIN_GRANULARITY)
{
    if (tasks.empty()) return {};

    // --- Runqueue entry ---
    struct RQEntry {
        double vruntime;
        int    pi; // index into sorted_tasks
        bool operator<(const RQEntry& o) const {
            if (vruntime != o.vruntime) return vruntime < o.vruntime;
            return pi < o.pi;
        }
    };

    // Per-task state
    struct TState {
        int    remaining;
        int    weight;
        double vruntime;
        bool   in_rq;
        bool   done;
    };

    const int n = static_cast<int>(tasks.size());
    std::vector<Task> sorted_tasks = tasks;
    std::sort(sorted_tasks.begin(), sorted_tasks.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });

    std::vector<TState> state(n);
    for (int i = 0; i < n; i++) {
        state[i] = {sorted_tasks[i].burst, sorted_tasks[i].weight(), 0.0, false, false};
    }

    std::set<RQEntry> rq; // ordered by vruntime
    double min_vrt = 0.0; // global minimum vruntime
    std::vector<Segment> tl;
    double cur = sorted_tasks[0].arrival;
    int idx = 0;

    // Total weight of processes in the runqueue
    auto total_weight = [&]() {
        int tw = 0;
        for (const auto& e : rq) {
            tw += state[e.pi].weight;
        }
        return tw == 0 ? 1 : tw;
    };

    // Admit processes that have arrived by time t
    auto admit = [&](double t) {
        while (idx < n && sorted_tasks[idx].arrival <= t) {
            int pi = idx;
            // Set vruntime: new process inherits min_vruntime (minus one target_latency
            // so it gets to run soon, but doesn't immediately starve others)
            double vrt = std::max(0.0, min_vrt - target_latency);
            state[pi].vruntime = vrt;
            state[pi].in_rq    = true;
            rq.insert({vrt, pi});
            idx++;
        }
    };

    admit(cur);

    while (true) {
        if (rq.empty()) {
            if (idx < n) {
                cur = sorted_tasks[idx].arrival;
                admit(cur);
                continue;
            }
            break;
        }

        // Pick the task with smallest vruntime
        auto it = rq.begin();
        int pi = it->pi;
        int tw = total_weight(); // runnable total weight including pi
        rq.erase(it);

        int w  = state[pi].weight;

        // Compute slice: (target_latency * w) / total_runnable_weight
        double slice = (target_latency * w) / tw;
        if (slice < min_granularity) slice = min_granularity;

        // Clamp to remaining work
        double actual = std::min(slice, static_cast<double>(state[pi].remaining));

        // Check for next arrival preemption
        double next_arr = (idx < n) ? static_cast<double>(sorted_tasks[idx].arrival) : INF_TIME;
        if (next_arr < cur + actual) {
            // Preempt at next arrival (re-evaluate then)
            actual = next_arr - cur;
            if (actual <= 0) {
                // Admit the new process then re-pick
                admit(cur);
                rq.insert({state[pi].vruntime, pi});
                continue;
            }
        }

        tl.push_back({sorted_tasks[pi].id, cur, cur + actual});
        double delta_vrt = actual * static_cast<double>(WEIGHT_NICE0) / w;
        state[pi].vruntime  += delta_vrt;
        state[pi].remaining -= static_cast<int>(actual);
        cur += actual;

        // Update min_vruntime (monotonically)
        if (!rq.empty()) {
            min_vrt = std::max(min_vrt, rq.begin()->vruntime);
        }
        min_vrt = std::max(min_vrt, state[pi].vruntime - target_latency);

        admit(cur);

        if (state[pi].remaining > 0) {
            state[pi].in_rq = true;
            rq.insert({state[pi].vruntime, pi});
        } else {
            state[pi].done = true;
        }
    }
    return tl;
}

// ============================================================================
//  Main — Interactive mode: run all algorithms and compare
// ============================================================================

static std::vector<Task> readTasks() {
    int n;
    std::cout << "Number of processes: ";
    std::cin >> n;
    if (n <= 0) throw std::invalid_argument("process count must be > 0");

    std::vector<Task> tasks(n);
    std::cout << "Enter (arrival_time  burst_time  nice_value[-20..19])"
                 "  for each process (nice=0 for default):\n";
    for (int i = 0; i < n; i++) {
        tasks[i].id = i;
        std::cout << "  P" << i << ": ";
        std::cin >> tasks[i].arrival >> tasks[i].burst >> tasks[i].nice;
        if (tasks[i].arrival < 0 || tasks[i].burst <= 0)
            throw std::invalid_argument("arrival>=0 and burst>0 required");
    }
    return tasks;
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║       OS Scheduler — Simulation Engine           ║\n";
    std::cout << "║  Algorithms: FCFS | SJF | SRTF | RR | MLFQ | CFS║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";

    try {
        auto tasks = readTasks();

        int quantum;
        std::cout << "Time quantum for RR/MLFQ (default=" << DEFAULT_QUANTUM << "): ";
        std::cin >> quantum;
        if (quantum <= 0) quantum = DEFAULT_QUANTUM;

        std::vector<RunSummary> summaries;

        auto run = [&](const std::string& name, const std::vector<Segment>& tl) {
            auto res = buildResults(tasks, tl);
            std::cout << "\n[ " << name << " ]\n";
            printResultsTable(res);
            printGantt(tl);
            auto s = buildSummary(name, tl, res);
            summaries.push_back(s);
        };

        run("FCFS",                  fcfs(tasks));
        run("SJF",                   sjf(tasks));
        run("SRTF",                  srtf(tasks));
        run("RR(q="+std::to_string(quantum)+")",
                                     roundRobin(tasks, quantum));
        run("MLFQ(q="+std::to_string(quantum)+")",
                                     mlfq(tasks, quantum));
        run("CFS",                   cfs(tasks));

        printComparison(summaries);

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
