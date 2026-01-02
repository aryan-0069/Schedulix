/**
 * @file   common.hpp
 * @brief  Shared data structures, constants, and utilities for the OS Scheduler project.
 *
 *  All scheduling algorithms (simulated + real) share these types so that
 *  results from different back-ends can be compared on a level playing field.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

// ─── Time constants ───────────────────────────────────────────────────────────

static constexpr int    INF_TIME  = 1'000'000'000;   // sentinel "no arrival"
static constexpr double INF_REAL  = 1e18;

// Default scheduling parameters (all in ms for the real backend,
// unitless ticks for the simulator)
static constexpr int    DEFAULT_QUANTUM      = 4;     // RR / MLFQ L0 quantum
static constexpr int    MLFQ_LEVELS          = 3;
static constexpr int    MLFQ_BOOST_INTERVAL  = 100;  // ticks between priority boosts
static constexpr double CFS_TARGET_LATENCY   = 20.0; // ms - CFS scheduling period
static constexpr double CFS_MIN_GRANULARITY  = 1.0;  // ms - minimum slice

// ─── nice to weight table (mirrors Linux sched.c, indices 0-39 for nice -20..19)
static constexpr int NICE_TO_WEIGHT[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */  9548,  7620,  6100,  4904,  3906,
    /*  -5 */  3121,  2501,  1991,  1586,  1277,
    /*   0 */  1024,   820,   655,   526,   423,
    /*   5 */   335,   272,   215,   172,   137,
    /*  10 */   110,    87,    70,    56,    45,
    /*  15 */    36,    29,    23,    18,    15
};
static constexpr int WEIGHT_NICE0 = 1024;

// Convert nice value (-20..19) to weight
static inline int niceToWeight(int nice) {
    int idx = nice + 20;
    if (idx < 0) idx = 0;
    if (idx > 39) idx = 39;
    return NICE_TO_WEIGHT[idx];
}

// ─── Core data structures ────────────────────────────────────────────────────

/**
 * A process specification - the immutable input.
 */
struct Task {
    int id;            // unique process ID (0-based)
    int arrival;       // arrival time (ticks / ms)
    int burst;         // CPU burst (ticks / ms) - total work to do
    int nice = 0;      // nice value [-20, 19]; 0 = default priority
    int priority = 0;  // static priority used by MLFQ at admission (0 = highest)

    int weight() const { return niceToWeight(nice); }
};

/**
 * One contiguous CPU slice: "process `id` held the CPU from `start` to `end`."
 * Both simulated and real back-ends build a timeline of these.
 */
struct Segment {
    int    id;
    double start;   // inclusive
    double end;     // exclusive
    int    level = 0; // MLFQ queue level this slice ran in (0 = highest)

    Segment() = default;
    Segment(int id_, double start_, double end_, int level_ = 0)
        : id(id_), start(start_), end(end_), level(level_) {}

    double duration() const { return end - start; }
};

/**
 * Per-task completion record - derived once the task exits.
 */
struct TaskResult {
    int    id;
    int    arrival;
    int    burst;
    double start_time;       // first time on CPU
    double completion_time;
    double turnaround;       // completion - arrival
    double waiting;          // turnaround - burst
    double response;         // start_time - arrival
    int    context_switches; // how many times this task was preempted + resumed
};

/**
 * Summary statistics for one algorithm run.
 */
struct RunSummary {
    std::string name;
    double avg_turnaround  = 0;
    double avg_waiting     = 0;
    double avg_response    = 0;
    double cpu_utilization = 0;
    int    context_switches = 0;
    double jains_fairness  = 0;   // Jain's Fairness Index on CPU share
    double makespan        = 0;   // total wall time from first arrival to last finish
};

// ─── Jain's Fairness Index ────────────────────────────────────────────────────
// J = (Sxi)^2 / (n * Sxi^2)   where xi = actual_cpu_i / ideal_cpu_i

inline double jainsFairnessIndex(const std::vector<TaskResult>& results) {
    if (results.empty()) return 1.0;
    int n = static_cast<int>(results.size());
    double sum = 0, sum_sq = 0;
    for (const auto& r : results) {
        double x = (r.turnaround > 0) ? static_cast<double>(r.burst) / r.turnaround : 1.0;
        sum    += x;
        sum_sq += x * x;
    }
    if (sum_sq == 0) return 1.0;
    return (sum * sum) / (n * sum_sq);
}

// ─── Build TaskResults from timeline ─────────────────────────────────────────

inline std::vector<TaskResult> buildResults(
    const std::vector<Task>&    tasks,
    const std::vector<Segment>& timeline)
{
    std::vector<TaskResult> out;
    out.reserve(tasks.size());

    for (const auto& t : tasks) {
        TaskResult r{};
        r.id      = t.id;
        r.arrival = t.arrival;
        r.burst   = t.burst;
        r.start_time       = INF_REAL;
        r.completion_time  = 0;
        r.context_switches = 0;

        int prev_id = -99;
        bool found = false;
        for (const auto& seg : timeline) {
            if (seg.id != t.id) { prev_id = seg.id; continue; }
            found = true;
            r.start_time      = std::min(r.start_time,      seg.start);
            r.completion_time = std::max(r.completion_time, seg.end);
            if (prev_id != t.id && prev_id != -99) r.context_switches++;
            prev_id = t.id;
        }
        if (!found) r.start_time = static_cast<double>(t.arrival);

        r.turnaround = r.completion_time - t.arrival;
        r.waiting    = r.turnaround - t.burst;
        r.response   = r.start_time  - t.arrival;
        out.push_back(r);
    }

    std::sort(out.begin(), out.end(), [](const TaskResult& a, const TaskResult& b){
        return a.id < b.id;
    });
    return out;
}

// ─── Summary builder ─────────────────────────────────────────────────────────

inline RunSummary buildSummary(
    const std::string&             name,
    const std::vector<Segment>&    timeline,
    const std::vector<TaskResult>& results)
{
    RunSummary s;
    s.name = name;
    if (results.empty()) return s;

    double tot_tat = 0, tot_wt = 0, tot_rt = 0, busy = 0;
    for (const auto& r : results) {
        tot_tat += r.turnaround;
        tot_wt  += r.waiting;
        tot_rt  += r.response;
    }
    for (const auto& seg : timeline)
        busy += seg.duration();

    int n = static_cast<int>(results.size());
    s.avg_turnaround   = tot_tat / n;
    s.avg_waiting      = tot_wt  / n;
    s.avg_response     = tot_rt  / n;
    s.jains_fairness   = jainsFairnessIndex(results);

    s.context_switches = 0;
    for (size_t i = 1; i < timeline.size(); i++)
        if (timeline[i].id != timeline[i-1].id) s.context_switches++;

    if (!timeline.empty()) {
        double tstart = timeline.front().start;
        double tend   = timeline.back().end;
        s.makespan        = tend - tstart;
        s.cpu_utilization = s.makespan > 0 ? (busy / s.makespan) * 100.0 : 0;
    }
    return s;
}

// ─── Gantt Chart ─────────────────────────────────────────────────────────────

inline void printGantt(const std::vector<Segment>& timeline, bool use_ms = false) {
    if (timeline.empty()) { std::cout << "(empty timeline)\n"; return; }

    std::vector<Segment> merged;
    merged.push_back(timeline[0]);
    for (size_t i = 1; i < timeline.size(); i++) {
        auto& back = merged.back();
        if (timeline[i].id == back.id && timeline[i].start <= back.end + 1e-9)
            back.end = timeline[i].end;
        else
            merged.push_back(timeline[i]);
    }

    const int SCALE = 2, MIN_W = 5;
    std::string border, label_row, time_row;
    border += '+';
    double prev_end = merged[0].start;

    auto fmt_t = [&](double t) -> std::string {
        std::ostringstream os;
        if (use_ms) os << std::fixed << std::setprecision(0) << t;
        else        os << static_cast<int>(t);
        return os.str();
    };

    for (const auto& seg : merged) {
        if (seg.start > prev_end + 1e-9) {
            int gap_w = std::max(MIN_W, static_cast<int>((seg.start - prev_end) * SCALE));
            border += std::string(gap_w, '-') + '+';
            std::string idle = "IDLE";
            int lpad = (gap_w - static_cast<int>(idle.size())) / 2;
            if (lpad < 0) lpad = 0;
            int rpad = gap_w - static_cast<int>(idle.size()) - lpad;
            if (rpad < 0) rpad = 0;
            label_row += '|' + std::string(lpad, ' ') + idle + std::string(rpad, ' ');
            std::string ts = fmt_t(prev_end);
            int sp = gap_w + 1 - static_cast<int>(ts.size());
            time_row += ts + std::string(std::max(1,sp), ' ');
        }
        double dur = seg.end - seg.start;
        int w = std::max(MIN_W, static_cast<int>(dur * SCALE));
        std::string lbl = "P" + std::to_string(seg.id);
        int pad = w - static_cast<int>(lbl.size()), lpad = pad / 2;
        if (lpad < 0) lpad = 0;
        int rpad = pad - lpad;
        if (rpad < 0) rpad = 0;
        border    += std::string(w, '-') + '+';
        label_row += '|' + std::string(lpad, ' ') + lbl + std::string(rpad, ' ');
        std::string ts = fmt_t(seg.start);
        int sp = w + 1 - static_cast<int>(ts.size());
        time_row += ts + std::string(std::max(1,sp), ' ');
        prev_end = seg.end;
    }
    label_row += '|';
    time_row  += fmt_t(merged.back().end);

    std::cout << "\nGantt Chart" << (use_ms ? " (ms)" : " (ticks)") << ":\n";
    std::cout << border << '\n' << label_row << '\n' << border << '\n' << time_row << "\n\n";
}

// ─── Results table ────────────────────────────────────────────────────────────

inline void printResultsTable(const std::vector<TaskResult>& results, bool use_ms = false) {
    const std::string sep(86, '-');
    const std::string u = use_ms ? "ms" : "t ";
    std::cout << '\n' << sep << '\n';
    std::cout << std::left
              << std::setw(6)  << "PID"
              << std::setw(9)  << ("AT("+u+")")
              << std::setw(9)  << ("BT("+u+")")
              << std::setw(10) << ("ST("+u+")")
              << std::setw(10) << ("CT("+u+")")
              << std::setw(10) << ("TAT("+u+")")
              << std::setw(10) << ("WT("+u+")")
              << std::setw(10) << ("RT("+u+")")
              << "Ctx-Sw\n" << sep << '\n';

    std::cout << std::fixed << std::setprecision(1);
    double tot_tat = 0, tot_wt = 0, tot_rt = 0;
    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(6)  << r.id
                  << std::setw(9)  << r.arrival
                  << std::setw(9)  << r.burst
                  << std::setw(10) << r.start_time
                  << std::setw(10) << r.completion_time
                  << std::setw(10) << r.turnaround
                  << std::setw(10) << r.waiting
                  << std::setw(10) << r.response
                  << r.context_switches << '\n';
        tot_tat += r.turnaround;
        tot_wt  += r.waiting;
        tot_rt  += r.response;
    }
    int n = static_cast<int>(results.size());
    std::cout << sep << '\n'
              << "Avg TAT: " << tot_tat/n << "  "
              << "Avg WT: "  << tot_wt/n  << "  "
              << "Avg RT: "  << tot_rt/n  << '\n'
              << sep << '\n';
}

// ─── Comparison table ────────────────────────────────────────────────────────

inline void printComparison(const std::vector<RunSummary>& summaries) {
    if (summaries.empty()) return;
    constexpr int NW = 12, W = 13;
    const std::string sep(NW + 6*W, '=');
    std::cout << "\n\n" << sep << "\n"
              << "                         ALGORITHM COMPARISON\n" << sep << "\n";
    std::cout << std::left
              << std::setw(NW) << "Algorithm"
              << std::setw(W)  << "Avg TAT"
              << std::setw(W)  << "Avg WT"
              << std::setw(W)  << "Avg RT"
              << std::setw(W)  << "Ctx-Sw"
              << std::setw(W)  << "CPU Util%"
              << std::setw(W)  << "Jain's J\n";
    std::cout << std::string(NW + 6*W, '-') << '\n';
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& s : summaries) {
        std::cout << std::left
                  << std::setw(NW) << s.name
                  << std::setw(W)  << s.avg_turnaround
                  << std::setw(W)  << s.avg_waiting
                  << std::setw(W)  << s.avg_response
                  << std::setw(W)  << s.context_switches
                  << std::setw(W)  << s.cpu_utilization
                  << std::setw(W)  << s.jains_fairness << '\n';
    }
    std::cout << sep << '\n';

    auto best_min = [&](auto fn, const std::string& label) {
        auto it = std::min_element(summaries.begin(), summaries.end(), fn);
        std::cout << label << ": " << it->name << '\n';
    };
    auto best_max = [&](auto fn, const std::string& label) {
        auto it = std::max_element(summaries.begin(), summaries.end(), fn);
        std::cout << label << ": " << it->name << '\n';
    };
    best_min([](const RunSummary& a, const RunSummary& b){ return a.avg_turnaround < b.avg_turnaround; },
             "Best Avg Turnaround");
    best_min([](const RunSummary& a, const RunSummary& b){ return a.avg_waiting    < b.avg_waiting;    },
             "Best Avg Waiting   ");
    best_max([](const RunSummary& a, const RunSummary& b){ return a.jains_fairness < b.jains_fairness; },
             "Fairest (Jain's J) ");
    std::cout << sep << '\n';
}
