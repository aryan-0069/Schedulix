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

// ─── Performance Metrics ─────────────────────────────────────────────────────

static void printResults(const std::vector<ProcessResult>& results) {
    constexpr int W = 10;
    const std::string sep(74, '-');
    std::cout << '\n' << sep << '\n';
    std::cout << std::left
              << std::setw(W) << "PID" << std::setw(W) << "AT"
              << std::setw(W) << "BT"  << std::setw(W) << "ST"
              << std::setw(W) << "CT"  << std::setw(W) << "TAT"
              << std::setw(W) << "WT"  << '\n';
    std::cout << sep << '\n';
    double total_tat = 0, total_wt = 0;
    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(W) << r.pid            << std::setw(W) << r.arrival_time
                  << std::setw(W) << r.burst_time      << std::setw(W) << r.start_time
                  << std::setw(W) << r.completion_time << std::setw(W) << r.turnaround_time
                  << std::setw(W) << r.waiting_time    << '\n';
        total_tat += r.turnaround_time;
        total_wt  += r.waiting_time;
    }
    std::cout << sep << '\n';
    int n = static_cast<int>(results.size());
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Average Turnaround Time : " << total_tat / n << '\n';
    std::cout << "Average Waiting Time    : " << total_wt  / n << '\n';
    std::cout << sep << '\n';
}

static int countContextSwitches(const std::vector<ExecutionSegment>& timeline) {
    if (timeline.empty()) return 0;
    int switches = 0;
    const int n = static_cast<int>(timeline.size());
    for (int i = 1; i < n; i++)
        if (timeline[i].pid != timeline[i-1].pid)
            switches++;
    return switches;
}

static double calculateCpuUtilization(const std::vector<ExecutionSegment>& timeline) {
    if (timeline.empty()) return 0.0;
    int busy = 0;
    for (const auto& seg : timeline)
        busy += seg.end - seg.start;
    int total = timeline.back().end - timeline.front().start;
    return total == 0 ? 0.0 : (static_cast<double>(busy) / total) * 100.0;
}

/**
 * Builds an AlgorithmSummary from a completed run's results + timeline.
 * This is what feeds the final side-by-side comparison table.
 */
static AlgorithmSummary summarize(const std::string& name,
                                   const std::vector<ProcessResult>& results,
                                   const std::vector<ExecutionSegment>& timeline) {
    double total_tat = 0, total_wt = 0;
    for (const auto& r : results) {
        total_tat += r.turnaround_time;
        total_wt  += r.waiting_time;
    }
    int n = static_cast<int>(results.size());
    return AlgorithmSummary{
        name,
        n ? total_tat / n : 0.0,
        n ? total_wt  / n : 0.0,
        countContextSwitches(timeline),
        calculateCpuUtilization(timeline)
    };
}

static void printGanttChart(const std::vector<ExecutionSegment>& timeline) {
    if (timeline.empty()) { std::cout << "(empty timeline)\n"; return; }

    // Merge consecutive same-pid segments (SRTF/RR produce many tiny slices)
    std::vector<ExecutionSegment> merged;
    merged.push_back(timeline[0]);
    for (size_t i = 1; i < timeline.size(); i++) {
        if (timeline[i].pid == merged.back().pid && timeline[i].start == merged.back().end)
            merged.back().end = timeline[i].end;
        else
            merged.push_back(timeline[i]);
    }

    const int SCALE = 2, MIN_W = 4;
    std::string border, label_row, time_row;
    border += '+';
    int prev_end = merged[0].start;

    for (const auto& seg : merged) {
        // Idle gap
        if (seg.start > prev_end) {
            int width = std::max(MIN_W, (seg.start - prev_end) * SCALE);
            border    += std::string(width, '-') + '+';
            label_row += '|' + std::string((width - 4) / 2, ' ') + "idle"
                       + std::string(width - 4 - (width - 4) / 2, ' ');
            std::string ts = std::to_string(prev_end);
            time_row += ts + std::string(width + 1 - static_cast<int>(ts.size()), ' ');
        }
        // Process bar
        int width = std::max(MIN_W, (seg.end - seg.start) * SCALE);
        std::string lbl = "P" + std::to_string(seg.pid);
        int pad = width - static_cast<int>(lbl.size()), lpad = pad / 2;
        border    += std::string(width, '-') + '+';
        label_row += '|' + std::string(lpad, ' ') + lbl + std::string(pad - lpad, ' ');
        std::string ts = std::to_string(seg.start);
        time_row += ts + std::string(width + 1 - static_cast<int>(ts.size()), ' ');
        prev_end = seg.end;
    }
    label_row += '|';
    time_row  += std::to_string(merged.back().end);

    std::cout << "\nGantt Chart:\n";
    std::cout << border << '\n' << label_row << '\n' << border << '\n' << time_row << '\n';
}

/**
 * Prints a side-by-side comparison table across all algorithms that ran,
 * and calls out which one wins on avg TAT and avg WT.
 */
static void printComparison(const std::vector<AlgorithmSummary>& summaries) {
    if (summaries.empty()) return;

    constexpr int NAME_W = 10, W = 14;
    const std::string sep(NAME_W + 4 * W, '=');

    std::cout << "\n\n" << sep << '\n';
    std::cout << "                    ALGORITHM COMPARISON\n";
    std::cout << sep << '\n';
    std::cout << std::left << std::setw(NAME_W) << "Algo"
              << std::setw(W) << "Avg TAT" << std::setw(W) << "Avg WT"
              << std::setw(W) << "Ctx Sw." << std::setw(W) << "CPU Util(%)" << '\n';
    std::cout << std::string(NAME_W + 4 * W, '-') << '\n';

    std::cout << std::fixed << std::setprecision(2);
    for (const auto& s : summaries) {
        std::cout << std::left << std::setw(NAME_W) << s.name
                  << std::setw(W) << s.avg_turnaround_time
                  << std::setw(W) << s.avg_waiting_time
                  << std::setw(W) << s.context_switches
                  << std::setw(W) << s.cpu_utilization << '\n';
    }
    std::cout << sep << '\n';

    auto best_tat = std::min_element(summaries.begin(), summaries.end(),
        [](const AlgorithmSummary& a, const AlgorithmSummary& b) {
            return a.avg_turnaround_time < b.avg_turnaround_time;
        });
    auto best_wt = std::min_element(summaries.begin(), summaries.end(),
        [](const AlgorithmSummary& a, const AlgorithmSummary& b) {
            return a.avg_waiting_time < b.avg_waiting_time;
        });

    std::cout << "Best Avg Turnaround Time : " << best_tat->name
              << " (" << best_tat->avg_turnaround_time << ")\n";
    std::cout << "Best Avg Waiting Time    : " << best_wt->name
              << " (" << best_wt->avg_waiting_time << ")\n";
    std::cout << sep << '\n';
}

// ─── Scheduling Algorithms ───────────────────────────────────────────────────

static std::vector<ProcessResult> fcfs(std::vector<Process> procs,
                                        std::vector<ExecutionSegment>& timeline) {
    sortByArrival(procs);
    std::vector<ProcessResult> results;
    results.reserve(procs.size());
    int cur_time = 0;
    for (const auto& p : procs) {
        int start  = std::max(cur_time, p.arrival_time);
        int finish = start + p.burst_time;
        int tat = finish - p.arrival_time, wt = tat - p.burst_time;
        results.push_back({p.pid, p.arrival_time, p.burst_time, start, finish, tat, wt});
        timeline.push_back({p.pid, start, finish});
        cur_time = finish;
    }
    return results;
}

static std::vector<ProcessResult> sjf(std::vector<Process> procs,
                                       std::vector<ExecutionSegment>& timeline) {
    sortByArrival(procs);
    const int n = static_cast<int>(procs.size());
    std::vector<ProcessResult> results;
    results.reserve(n);

    auto cmp = [](const std::pair<int,int>& a, const std::pair<int,int>& b) {
        return a.first > b.first || (a.first == b.first && a.second > b.second);
    };
    std::priority_queue<std::pair<int,int>, std::vector<std::pair<int,int>>, decltype(cmp)> ready(cmp);

    std::vector<Process> by_pid(n);
    for (const auto& p : procs) by_pid[p.pid] = p;

    int cur_time = 0, idx = 0;
    std::vector<int> completion_time(n, -1), start_time(n, -1);

    while (idx < n || !ready.empty()) {
        while (idx < n && procs[idx].arrival_time <= cur_time) {
            ready.push({procs[idx].burst_time, procs[idx].pid});
            ++idx;
        }
        if (ready.empty()) { cur_time = procs[idx].arrival_time; continue; }
        auto [bt, pid] = ready.top(); ready.pop();
        start_time[pid] = cur_time;
        cur_time += bt;
        completion_time[pid] = cur_time;
        timeline.push_back({pid, start_time[pid], completion_time[pid]});
    }

    for (int pid = 0; pid < n; ++pid) {
        int tat = completion_time[pid] - by_pid[pid].arrival_time;
        int wt  = tat - by_pid[pid].burst_time;
        results.push_back({pid, by_pid[pid].arrival_time, by_pid[pid].burst_time,
                           start_time[pid], completion_time[pid], tat, wt});
    }
    std::sort(results.begin(), results.end(),
              [](const ProcessResult& a, const ProcessResult& b){ return a.pid < b.pid; });
    return results;
}

static std::vector<ProcessResult> srtf(std::vector<Process> procs,
                                        std::vector<ExecutionSegment>& timeline) {
    sortByArrival(procs);
    const int n = static_cast<int>(procs.size());
    std::vector<int> remaining(n), completion_time(n, -1), start_time(n, -1);
    for (int i = 0; i < n; ++i) remaining[procs[i].pid] = procs[i].burst_time;

    auto cmp = [](const std::pair<int,int>& a, const std::pair<int,int>& b) {
        return a.first > b.first || (a.first == b.first && a.second > b.second);
    };
    std::priority_queue<std::pair<int,int>, std::vector<std::pair<int,int>>, decltype(cmp)> ready(cmp);

    int cur_time = 0, idx = 0;

    while (idx < n || !ready.empty()) {
        while (idx < n && procs[idx].arrival_time <= cur_time) {
            ready.push({remaining[procs[idx].pid], procs[idx].pid});
            ++idx;
        }
        if (ready.empty()) { cur_time = procs[idx].arrival_time; continue; }

        int next_arrival = (idx < n) ? procs[idx].arrival_time : INF;
        auto [rem, pid] = ready.top(); ready.pop();
        if (start_time[pid] == -1) start_time[pid] = cur_time;

        int seg_start = cur_time;
        int run       = std::min(next_arrival - cur_time, rem);
        cur_time       += run;
        remaining[pid] -= run;
        timeline.push_back({pid, seg_start, cur_time});

        if (remaining[pid] == 0) completion_time[pid] = cur_time;
        else ready.push({remaining[pid], pid});
    }

    std::vector<Process> by_pid(n);
    for (const auto& p : procs) by_pid[p.pid] = p;
    std::vector<ProcessResult> results;
    results.reserve(n);
    for (int pid = 0; pid < n; ++pid) {
        int tat = completion_time[pid] - by_pid[pid].arrival_time;
        int wt  = tat - by_pid[pid].burst_time;
        results.push_back({pid, by_pid[pid].arrival_time, by_pid[pid].burst_time,
                           start_time[pid], completion_time[pid], tat, wt});
    }
    return results;
}

static std::vector<ProcessResult> roundRobin(std::vector<Process> procs, int quantum,
                                              std::vector<ExecutionSegment>& timeline) {
    if (quantum <= 0) throw std::invalid_argument("Quantum must be positive.");
    sortByArrival(procs);
    const int n = static_cast<int>(procs.size());
    std::vector<int> remaining(n), completion_time(n, -1), start_time(n, -1);
    for (int i = 0; i < n; ++i) remaining[procs[i].pid] = procs[i].burst_time;

    std::deque<int> ready;
    int cur_time = 0, idx = 0;

    if (!procs.empty()) {
        cur_time = procs[0].arrival_time;
        while (idx < n && procs[idx].arrival_time <= cur_time)
            ready.push_back(procs[idx++].pid);
    }

    while (!ready.empty()) {
        int pid = ready.front(); ready.pop_front();
        if (start_time[pid] == -1) start_time[pid] = cur_time;

        int seg_start = cur_time;
        int run       = std::min(quantum, remaining[pid]);
        cur_time       += run;
        remaining[pid] -= run;
        int seg_end = cur_time;

        while (idx < n && procs[idx].arrival_time <= cur_time)
            ready.push_back(procs[idx++].pid);

        if (remaining[pid] == 0) completion_time[pid] = cur_time;
        else ready.push_back(pid);

        timeline.push_back({pid, seg_start, seg_end});

        if (ready.empty() && idx < n) {
            cur_time = procs[idx].arrival_time;
            while (idx < n && procs[idx].arrival_time <= cur_time)
                ready.push_back(procs[idx++].pid);
        }
    }

    std::vector<Process> by_pid(n);
    for (const auto& p : procs) by_pid[p.pid] = p;
    std::vector<ProcessResult> results;
    results.reserve(n);
    for (int pid = 0; pid < n; ++pid) {
        int tat = completion_time[pid] - by_pid[pid].arrival_time;
        int wt  = tat - by_pid[pid].burst_time;
        results.push_back({pid, by_pid[pid].arrival_time, by_pid[pid].burst_time,
                           start_time[pid], completion_time[pid], tat, wt});
    }
    return results;
}

// ─── Input ────────────────────────────────────────────────────────────────────

static std::vector<Process> readProcesses() {
    int n;
    std::cout << "Number of processes: ";
    std::cin >> n;
    if (n <= 0) throw std::invalid_argument("Process count must be positive.");
    std::vector<Process> procs(n);
    std::cout << "Enter (arrival_time burst_time) for each process:\n";
    for (int i = 0; i < n; ++i) {
        procs[i].pid = i;
        std::cout << "  P" << i << ": ";
        std::cin >> procs[i].arrival_time >> procs[i].burst_time;
        if (procs[i].arrival_time < 0 || procs[i].burst_time <= 0)
            throw std::invalid_argument("Arrival time must be >= 0 and burst time must be > 0.");
    }
    return procs;
}

// ─── Entry Point ─────────────────────────────────────────────────────────────

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║      CPU Scheduling Simulator        ║\n";
    std::cout << "║   Runs FCFS, SJF, SRTF & RR and      ║\n";
    std::cout << "║   compares them side by side         ║\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";

    try {
        std::vector<Process> procs = readProcesses();

        int quantum;
        std::cout << "Time quantum (for Round Robin): ";
        std::cin >> quantum;
        if (quantum <= 0)
            throw std::invalid_argument("Quantum must be positive.");

        std::vector<AlgorithmSummary> summaries;

        // Each algorithm gets its own fresh timeline; `procs` is passed by value
        // into each function, so the same original process set feeds all four.

        {
            std::cout << "\n[ FCFS Scheduling ]\n";
            std::vector<ExecutionSegment> timeline;
            auto results = fcfs(procs, timeline);
            printResults(results);
            printGanttChart(timeline);
            std::cout << "Context Switches : " << countContextSwitches(timeline) << '\n';
            std::cout << "CPU Utilization  : " << std::fixed << std::setprecision(2)
                      << calculateCpuUtilization(timeline) << "%\n";
            summaries.push_back(summarize("FCFS", results, timeline));
        }
        {
            std::cout << "\n[ SJF Scheduling (Non-Preemptive) ]\n";
            std::vector<ExecutionSegment> timeline;
            auto results = sjf(procs, timeline);
            printResults(results);
            printGanttChart(timeline);
            std::cout << "Context Switches : " << countContextSwitches(timeline) << '\n';
            std::cout << "CPU Utilization  : " << std::fixed << std::setprecision(2)
                      << calculateCpuUtilization(timeline) << "%\n";
            summaries.push_back(summarize("SJF", results, timeline));
        }
        {
            std::cout << "\n[ SRTF Scheduling (Preemptive SJF) ]\n";
            std::vector<ExecutionSegment> timeline;
            auto results = srtf(procs, timeline);
            printResults(results);
            printGanttChart(timeline);
            std::cout << "Context Switches : " << countContextSwitches(timeline) << '\n';
            std::cout << "CPU Utilization  : " << std::fixed << std::setprecision(2)
                      << calculateCpuUtilization(timeline) << "%\n";
            summaries.push_back(summarize("SRTF", results, timeline));
        }
        {
            std::cout << "\n[ Round Robin Scheduling | Quantum = " << quantum << " ]\n";
            std::vector<ExecutionSegment> timeline;
            auto results = roundRobin(procs, quantum, timeline);
            printResults(results);
            printGanttChart(timeline);
            std::cout << "Context Switches : " << countContextSwitches(timeline) << '\n';
            std::cout << "CPU Utilization  : " << std::fixed << std::setprecision(2)
                      << calculateCpuUtilization(timeline) << "%\n";
            summaries.push_back(summarize("RR", results, timeline));
        }

        printComparison(summaries);

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}