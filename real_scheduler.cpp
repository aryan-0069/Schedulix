/**
 * @file   real_scheduler.cpp
 * @brief  Preemptive Real-Process Scheduler — Linux back-end.
 *
 *  Implements FCFS, Round Robin, MLFQ, and CFS scheduling using REAL Linux
 *  processes controlled via fork() + SIGSTOP + SIGCONT + timer-based preemption.
 *
 *  Architecture
 *  ────────────
 *   1. One child process is forked per task.  Each child immediately raises(SIGSTOP)
 *      to park itself.  The parent confirms the stop via waitpid(WUNTRACED).
 *   2. The scheduler loop uses POSIX interval timers (setitimer / SIGALRM) to
 *      enforce time quanta.  On SIGALRM, the running child is sent SIGSTOP; the
 *      parent waits for the stop acknowledgement, then picks the next task.
 *   3. Each child burns CPU in a tight loop for its burst duration, measured with
 *      CLOCK_PROCESS_CPUTIME_ID so SIGSTOP pauses do not count toward the budget.
 *   4. On completion, the child writes its actual finish timestamp through a pipe
 *      back to the parent.
 *   5. Metrics (real wall-clock ms) are recorded and compared against the
 *      simulator output for the same workload.
 *
 *  Supported algorithms:
 *    --fcfs  : FCFS (no preemption)
 *    --rr    : Round Robin with configurable quantum
 *    --mlfq  : Multi-Level Feedback Queue
 *    --cfs   : CFS-style proportional scheduling
 *    (default: runs all four and prints a comparison)
 *
 *  Compile:
 *    g++ -std=c++17 -O2 -Wall -o real_scheduler real_scheduler.cpp
 *  Run:
 *    ./real_scheduler [--fcfs|--rr|--mlfq|--cfs] [--quantum <ms>]
 *  Platform: Linux only.
 *
 *  Limitations / known behaviour:
 *    - Burst times must be large enough (>= 50 ms) that OS scheduler jitter
 *      does not dominate.  Use 100–2000 ms bursts for meaningful results.
 *    - The CFS implementation here uses proportional-weight quanta; it is NOT
 *      kernel CFS but an approximation at user-space granularity.
 *    - SIGALRM + setitimer provides ~1 ms resolution on modern Linux kernels.
 */

#ifndef __linux__
#include <iostream>

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   real_scheduler: Linux-only component                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Note: real_scheduler requires Linux kernel process controls:\n"
              << "  - fork(), waitpid(), raise(SIGSTOP), kill(SIGCONT)\n"
              << "  - POSIX interval timers (setitimer / SIGALRM)\n"
              << "  - CLOCK_PROCESS_CPUTIME_ID\n\n"
              << "To run this component on Windows:\n"
              << "  1. Use WSL (Windows Subsystem for Linux), Ubuntu, or a Linux VM.\n"
              << "  2. For cross-platform software simulation on Windows, run:\n"
              << "     ./scheduler_sim.exe\n";
    return 0;
}

#else

#include "common.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

// POSIX / Linux headers
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ─── Time helpers ─────────────────────────────────────────────────────────────

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms(double ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = static_cast<time_t>(ms / 1000.0);
    ts.tv_nsec = static_cast<long>((ms - ts.tv_sec * 1000.0) * 1e6);
    nanosleep(&ts, nullptr);
}

// ─── Signal helpers ────────────────────────────────────────────────────────────

static void xkill(pid_t pid, int sig) {
    if (kill(pid, sig) == -1 && errno != ESRCH) {
        std::perror("kill");
        std::abort();
    }
}

// Block until pid is confirmed stopped (WIFSTOPPED)
static void wait_for_stop(pid_t pid) {
    int status;
    while (true) {
        pid_t r = waitpid(pid, &status, WUNTRACED | WCONTINUED);
        if (r == -1 && errno == EINTR) continue;
        if (r == pid && WIFSTOPPED(status)) return;
    }
}

// Block until pid exits; returns exit code (-1 if signalled)
static int wait_for_exit(pid_t pid) {
    int status;
    while (true) {
        pid_t r = waitpid(pid, &status, WUNTRACED);
        if (r == -1 && errno == EINTR) continue;
        if (r == pid) {
            if (WIFEXITED(status))   return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -1;
            // Stopped unexpectedly (shouldn't happen in normal flow)
            xkill(pid, SIGCONT);
        }
    }
}

// ─── SIGALRM timer ────────────────────────────────────────────────────────────

// Set a one-shot SIGALRM after `ms` milliseconds (0 to disarm)
static void arm_timer(double ms) {
    struct itimerval itv{};
    if (ms > 0) {
        long sec  = static_cast<long>(ms / 1000.0);
        long usec = static_cast<long>((ms - sec * 1000.0) * 1000.0);
        itv.it_value.tv_sec  = sec;
        itv.it_value.tv_usec = usec;
    }
    setitimer(ITIMER_REAL, &itv, nullptr);
}

static volatile sig_atomic_t g_alarm_fired = 0;

static void sigalrm_handler(int) {
    g_alarm_fired = 1;
}

// ─── Child process workload ────────────────────────────────────────────────────
//
// Each child:
//   1. Raises SIGSTOP to park (parent confirms stop via waitpid before loop begins)
//   2. On SIGCONT, burns CPU for `burst_ms` milliseconds measured via
//      CLOCK_PROCESS_CPUTIME_ID (pauses during SIGSTOP are NOT counted)
//   3. Exits with _exit(0)
//
// Using CLOCK_PROCESS_CPUTIME_ID is critical: it only ticks while the process
// is actually scheduled, so SIGSTOP-based preemptions do not steal burst time.

[[noreturn]] static void child_main(int burst_ms) {
    // Park immediately
    raise(SIGSTOP);

    // Busy-wait for `burst_ms` of CPU time
    struct timespec start, cur;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    volatile long long dummy = 0;
    while (true) {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cur);
        double elapsed = (cur.tv_sec  - start.tv_sec)  * 1000.0
                       + (cur.tv_nsec - start.tv_nsec) / 1e6;
        if (elapsed >= burst_ms) break;
        ++dummy;
    }
    _exit(0);
}

// ─── Live process record ───────────────────────────────────────────────────────

struct LiveProc {
    int    id;
    pid_t  pid;
    int    arrival_ms;
    int    burst_ms;
    double first_run_ms  = -1.0;
    double finish_ms     = -1.0;
    bool   done          = false;

    // MLFQ state
    int    mlfq_level    = 0;
    int    allotment_rem = 0; // allotment remaining in current level (ms)

    // CFS state
    double vruntime      = 0.0;
    int    weight        = WEIGHT_NICE0;
};

// ─── Fork all children and park them ──────────────────────────────────────────

static std::vector<LiveProc> spawnAll(const std::vector<Task>& tasks) {
    std::vector<LiveProc> live;
    live.reserve(tasks.size());
    for (const auto& t : tasks) {
        pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("fork() failed: " + std::string(strerror(errno)));
        if (pid == 0) child_main(t.burst); // child — never returns
        wait_for_stop(pid);                // parent: confirm child is stopped
        LiveProc lp{};
        lp.id          = t.id;
        lp.pid         = pid;
        lp.arrival_ms  = t.arrival;
        lp.burst_ms    = t.burst;
        lp.weight      = t.weight();
        live.push_back(lp);
    }
    return live;
}

// Resume child, optionally set a preemption timer.
// Returns the wall-clock start time of this slice.
static double resume(LiveProc& p, double quantum_ms) {
    if (p.first_run_ms < 0) p.first_run_ms = now_ms();
    g_alarm_fired = 0;
    if (quantum_ms > 0) arm_timer(quantum_ms);
    xkill(p.pid, SIGCONT);
    return now_ms();
}

// Stop child after preemption (SIGALRM fired).
// Returns the wall-clock end of the slice.
static double preempt(LiveProc& p) {
    arm_timer(0);          // disarm timer
    xkill(p.pid, SIGSTOP); // stop child
    wait_for_stop(p.pid);  // confirm stop
    return now_ms();
}

// Wait for child to exit (no preemption — used by FCFS for the current slot).
// Returns wall-clock finish time.
static double run_to_completion(LiveProc& p) {
    arm_timer(0); // ensure no timer
    wait_for_exit(p.pid);
    p.finish_ms = now_ms();
    p.done      = true;
    return p.finish_ms;
}

// ============================================================================
//  Real-FCFS
// ============================================================================

static std::vector<Segment> real_fcfs(std::vector<LiveProc>& procs, double sched_start) {
    std::sort(procs.begin(), procs.end(), [](const LiveProc& a, const LiveProc& b){
        return a.arrival_ms < b.arrival_ms || (a.arrival_ms == b.arrival_ms && a.id < b.id);
    });

    std::vector<Segment> tl;
    double cur = sched_start;

    for (auto& p : procs) {
        double arr_abs = sched_start + p.arrival_ms;
        if (cur < arr_abs) { sleep_ms(arr_abs - cur); cur = now_ms(); }

        double seg_start = resume(p, 0);
        double seg_end   = run_to_completion(p);
        tl.push_back({p.id, seg_start - sched_start, seg_end - sched_start});
        cur = seg_end;
    }
    return tl;
}

// ============================================================================
//  Real-Round Robin
// ============================================================================

static std::vector<Segment> real_rr(std::vector<LiveProc>& procs,
                                     double sched_start,
                                     double quantum_ms)
{
    std::sort(procs.begin(), procs.end(), [](const LiveProc& a, const LiveProc& b){
        return a.arrival_ms < b.arrival_ms || (a.arrival_ms == b.arrival_ms && a.id < b.id);
    });

    std::deque<int> ready; // indices into procs
    std::vector<bool> in_rq(procs.size(), false);
    std::vector<Segment> tl;
    double cur = sched_start;
    int idx = 0;

    auto admit = [&](double t_abs) {
        while (idx < (int)procs.size() &&
               sched_start + procs[idx].arrival_ms <= t_abs) {
            if (!in_rq[idx] && !procs[idx].done) {
                ready.push_back(idx);
                in_rq[idx] = true;
            }
            idx++;
        }
    };

    admit(cur); // admit at t=0 relative (cur == sched_start)
    // Seed with processes at first arrival
    if (ready.empty() && idx < (int)procs.size()) {
        cur = sched_start + procs[0].arrival_ms;
        admit(cur);
    }

    while (!ready.empty()) {
        int i = ready.front(); ready.pop_front(); in_rq[i] = false;
        auto& p = procs[i];

        double seg_start_abs = resume(p, quantum_ms);

        // Wait for timer OR exit
        bool exited = false;
        {
            int status;
            while (!g_alarm_fired) {
                pid_t r = waitpid(p.pid, &status, WNOHANG | WUNTRACED);
                if (r == p.pid) {
                    if (WIFEXITED(status) || WIFSIGNALED(status)) {
                        exited = true;
                        break;
                    }
                }
                // Tiny yield to avoid 100% parent CPU
                struct timespec ns{0, 100000}; // 0.1 ms
                nanosleep(&ns, nullptr);
            }
        }

        double seg_end_abs;
        if (exited) {
            arm_timer(0);
            seg_end_abs = now_ms();
            p.finish_ms = seg_end_abs;
            p.done      = true;
        } else {
            seg_end_abs = preempt(p);
        }

        tl.push_back({p.id,
                      seg_start_abs - sched_start,
                      seg_end_abs   - sched_start});
        cur = seg_end_abs;

        // Admit newly arrived processes
        admit(cur);

        if (!p.done) {
            ready.push_back(i);
            in_rq[i] = true;
        }

        if (ready.empty() && idx < (int)procs.size()) {
            cur = sched_start + procs[idx].arrival_ms;
            admit(cur);
        }
    }
    return tl;
}

// ============================================================================
//  Real-MLFQ
// ============================================================================

static std::vector<Segment> real_mlfq(std::vector<LiveProc>& procs,
                                       double sched_start,
                                       double base_quantum_ms,
                                       int    levels        = MLFQ_LEVELS,
                                       double boost_interval_ms = 2000.0)
{
    std::sort(procs.begin(), procs.end(), [](const LiveProc& a, const LiveProc& b){
        return a.arrival_ms < b.arrival_ms;
    });

    const int n = static_cast<int>(procs.size());
    for (int i = 0; i < n; i++) {
        procs[i].mlfq_level    = 0;
        procs[i].allotment_rem = static_cast<int>(base_quantum_ms);
    }

    auto qLevel = [&](int l) -> double {
        if (l >= levels - 1) return 1000.0 * 3600; // FCFS level: huge quantum
        return base_quantum_ms * (1 << l);
    };

    std::vector<std::deque<int>> queues(levels);
    std::vector<bool> in_queue(n, false);
    std::vector<Segment> tl;
    double cur = sched_start;
    double last_boost = cur;
    int idx = 0;

    auto admit = [&](double t_abs) {
        while (idx < n && sched_start + procs[idx].arrival_ms <= t_abs) {
            if (!procs[idx].done && !in_queue[idx]) {
                procs[idx].mlfq_level    = 0;
                procs[idx].allotment_rem = static_cast<int>(qLevel(0));
                queues[0].push_back(idx);
                in_queue[idx] = true;
            }
            idx++;
        }
    };

    auto pick_level = [&]() -> int {
        for (int l = 0; l < levels; l++)
            if (!queues[l].empty()) return l;
        return -1;
    };

    admit(cur);

    while (true) {
        // Priority boost
        if (cur - last_boost >= boost_interval_ms) {
            last_boost = cur;
            for (int l = 1; l < levels; l++) {
                for (int pi : queues[l]) {
                    procs[pi].mlfq_level    = 0;
                    procs[pi].allotment_rem = static_cast<int>(qLevel(0));
                    queues[0].push_back(pi);
                    in_queue[pi] = true;
                }
                queues[l].clear();
            }
        }

        int lv = pick_level();
        if (lv < 0) {
            if (idx < n) {
                cur = sched_start + procs[idx].arrival_ms;
                admit(cur);
                continue;
            }
            break;
        }

        int pi = queues[lv].front();
        queues[lv].pop_front();
        in_queue[pi] = false;
        auto& p = procs[pi];

        double slice_ms = std::min(qLevel(lv), static_cast<double>(p.allotment_rem));
        double seg_start_abs = resume(p, slice_ms);

        bool exited = false;
        {
            int status;
            while (!g_alarm_fired) {
                pid_t r = waitpid(p.pid, &status, WNOHANG | WUNTRACED);
                if (r == p.pid && (WIFEXITED(status) || WIFSIGNALED(status))) {
                    exited = true; break;
                }
                struct timespec ns{0, 100000};
                nanosleep(&ns, nullptr);
            }
        }

        double seg_end_abs;
        if (exited) {
            arm_timer(0);
            seg_end_abs  = now_ms();
            p.finish_ms  = seg_end_abs;
            p.done       = true;
        } else {
            seg_end_abs = preempt(p);
        }

        double ran_ms = seg_end_abs - seg_start_abs;
        tl.push_back({p.id, seg_start_abs - sched_start, seg_end_abs - sched_start, lv});
        cur = seg_end_abs;

        admit(cur);

        if (!p.done) {
            p.allotment_rem -= static_cast<int>(ran_ms);
            if (p.allotment_rem <= 0) {
                int next_lv = std::min(lv + 1, levels - 1);
                p.mlfq_level    = next_lv;
                p.allotment_rem = static_cast<int>(qLevel(next_lv));
                queues[next_lv].push_back(pi);
                in_queue[pi] = true;
            } else {
                // Partial slice (preempted by higher-level arrival) — stay at level
                queues[lv].push_front(pi);
                in_queue[pi] = true;
            }
        }
    }
    return tl;
}

// ============================================================================
//  Real-CFS (proportional-weight time slicing)
// ============================================================================

static std::vector<Segment> real_cfs(std::vector<LiveProc>& procs,
                                      double sched_start,
                                      double target_latency_ms = CFS_TARGET_LATENCY,
                                      double min_gran_ms       = CFS_MIN_GRANULARITY)
{
    std::sort(procs.begin(), procs.end(), [](const LiveProc& a, const LiveProc& b){
        return a.arrival_ms < b.arrival_ms;
    });

    struct RQEntry {
        double vruntime;
        int    idx; // index into procs
        bool operator<(const RQEntry& o) const {
            if (vruntime != o.vruntime) return vruntime < o.vruntime;
            return idx < o.idx;
        }
    };

    const int n = static_cast<int>(procs.size());
    std::set<RQEntry> rq;
    double min_vrt = 0.0;
    std::vector<Segment> tl;
    double cur = sched_start;
    int idx = 0;

    auto total_weight = [&]() {
        int tw = 0;
        for (const auto& e : rq) tw += procs[e.idx].weight;
        return tw == 0 ? 1 : tw;
    };

    auto admit = [&](double t_abs) {
        while (idx < n && sched_start + procs[idx].arrival_ms <= t_abs) {
            double vrt = std::max(0.0, min_vrt - target_latency_ms);
            procs[idx].vruntime = vrt;
            rq.insert({vrt, idx});
            idx++;
        }
    };

    admit(cur);

    while (true) {
        if (rq.empty()) {
            if (idx < n) { cur = sched_start + procs[idx].arrival_ms; admit(cur); continue; }
            break;
        }

        auto it = rq.begin();
        int pi = it->idx;
        auto& p = procs[pi];

        int tw = total_weight(); // runnable total weight includes p
        rq.erase(it);

        double slice_ms = (target_latency_ms * p.weight) / tw;
        if (slice_ms < min_gran_ms) slice_ms = min_gran_ms;

        double seg_start_abs = resume(p, slice_ms);

        bool exited = false;
        {
            int status;
            while (!g_alarm_fired) {
                pid_t r = waitpid(p.pid, &status, WNOHANG | WUNTRACED);
                if (r == p.pid && (WIFEXITED(status) || WIFSIGNALED(status))) {
                    exited = true; break;
                }
                struct timespec ns{0, 100000};
                nanosleep(&ns, nullptr);
            }
        }

        double seg_end_abs;
        if (exited) {
            arm_timer(0);
            seg_end_abs = now_ms();
            p.finish_ms = seg_end_abs;
            p.done      = true;
        } else {
            seg_end_abs = preempt(p);
        }

        double ran_ms = seg_end_abs - seg_start_abs;
        tl.push_back({p.id, seg_start_abs - sched_start, seg_end_abs - sched_start});
        cur = seg_end_abs;

        double delta_vrt = ran_ms * static_cast<double>(WEIGHT_NICE0) / p.weight;
        p.vruntime += delta_vrt;

        if (!rq.empty()) min_vrt = std::max(min_vrt, rq.begin()->vruntime);
        min_vrt = std::max(min_vrt, p.vruntime - target_latency_ms);

        admit(cur);

        if (!p.done) {
            rq.insert({p.vruntime, pi});
        }
    }
    return tl;
}

// ─── Convert LiveProc to TaskResult for unified comparison ───────────────────

static std::vector<TaskResult> liveToResults(const std::vector<LiveProc>& procs,
                                              double sched_start) {
    std::vector<TaskResult> out;
    for (const auto& p : procs) {
        TaskResult r{};
        r.id              = p.id;
        r.arrival         = p.arrival_ms;
        r.burst           = p.burst_ms;
        r.start_time      = p.first_run_ms >= 0 ? p.first_run_ms - sched_start : 0;
        r.completion_time = p.finish_ms    >= 0 ? p.finish_ms    - sched_start : 0;
        r.turnaround      = r.completion_time - p.arrival_ms;
        r.waiting         = r.turnaround - p.burst_ms;
        r.response        = r.start_time  - p.arrival_ms;
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const TaskResult& a, const TaskResult& b){
        return a.id < b.id;
    });
    return out;
}

// ─── Input ────────────────────────────────────────────────────────────────────

static std::vector<Task> readTasks() {
    int n;
    std::cout << "Number of processes: ";
    std::cin >> n;
    if (n <= 0) throw std::invalid_argument("process count must be > 0");

    std::vector<Task> tasks(n);
    std::cout << "Enter (arrival_ms  burst_ms  nice[-20..19])"
                 " for each process (nice=0 for default priority):\n";
    for (int i = 0; i < n; i++) {
        tasks[i].id = i;
        std::cout << "  P" << i << ": ";
        std::cin >> tasks[i].arrival >> tasks[i].burst >> tasks[i].nice;
        if (tasks[i].arrival < 0 || tasks[i].burst <= 0)
            throw std::invalid_argument("arrival_ms>=0 and burst_ms>0 required");
    }
    return tasks;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    // Install SIGALRM handler
    struct sigaction sa{};
    sa.sa_handler = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // intentionally NOT SA_RESTART so that nanosleep returns on alarm
    sigaction(SIGALRM, &sa, nullptr);

    // Parse CLI
    bool run_fcfs_flag = false, run_rr_flag = false;
    bool run_mlfq_flag = false, run_cfs_flag = false;
    double quantum_ms = 100.0; // default 100 ms quantum

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "--fcfs") run_fcfs_flag = true;
        else if (a == "--rr")   run_rr_flag   = true;
        else if (a == "--mlfq") run_mlfq_flag = true;
        else if (a == "--cfs")  run_cfs_flag  = true;
        else if (a == "--quantum" && i+1 < argc) {
            quantum_ms = std::stod(argv[++i]);
        }
    }
    // If no flag specified, run all
    if (!run_fcfs_flag && !run_rr_flag && !run_mlfq_flag && !run_cfs_flag) {
        run_fcfs_flag = run_rr_flag = run_mlfq_flag = run_cfs_flag = true;
    }

    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Real Linux Process Scheduler                   ║\n";
    std::cout << "║   fork() + SIGSTOP/SIGCONT + SIGALRM preemption  ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";
    std::cout << "NOTE: Use burst_ms >= 50ms for meaningful wall-clock results.\n\n";

    try {
        auto tasks = readTasks();

        if (quantum_ms <= 0) {
            std::cout << "Quantum (ms) [default 100]: ";
            std::cin >> quantum_ms;
            if (quantum_ms <= 0) quantum_ms = 100.0;
        }

        std::vector<RunSummary> summaries;

        // ── Helper: spawn, run one algorithm, collect results, cleanup ──
        auto run_algo = [&](const std::string& name,
                            std::function<std::vector<Segment>(std::vector<LiveProc>&, double)> fn)
        {
            std::cout << "\n[ " << name << " — forking " << tasks.size() << " children ]\n";
            auto procs = spawnAll(tasks);
            double sched_start = now_ms();

            auto tl  = fn(procs, sched_start);
            auto res = liveToResults(procs, sched_start);

            // Kill any surviving children (safety net)
            for (auto& p : procs) {
                if (!p.done) {
                    xkill(p.pid, SIGKILL);
                    int st; waitpid(p.pid, &st, 0);
                }
            }

            printResultsTable(res, /*use_ms=*/true);
            printGantt(tl, /*use_ms=*/true);
            auto s = buildSummary(name, tl, res);
            summaries.push_back(s);
        };

        if (run_fcfs_flag)
            run_algo("Real-FCFS", [&](std::vector<LiveProc>& p, double s){
                return real_fcfs(p, s); });

        if (run_rr_flag)
            run_algo("Real-RR(q=" + std::to_string(static_cast<int>(quantum_ms)) + "ms)",
                     [&](std::vector<LiveProc>& p, double s){
                         return real_rr(p, s, quantum_ms); });

        if (run_mlfq_flag)
            run_algo("Real-MLFQ",
                     [&](std::vector<LiveProc>& p, double s){
                         return real_mlfq(p, s, quantum_ms); });

        if (run_cfs_flag)
            run_algo("Real-CFS",
                     [&](std::vector<LiveProc>& p, double s){
                         return real_cfs(p, s); });

        if (summaries.size() > 1) printComparison(summaries);

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif // __linux__
