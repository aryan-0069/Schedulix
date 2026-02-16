/**
 * @file   validator.cpp
 * @brief  Simulator ↔ Real-backend Validation Engine
 *
 *  Runs the SAME workload through both the pure simulator and the real
 *  Linux process scheduler, then reports the gap between predicted and
 *  measured metrics.
 *
 *  Validation metrics:
 *    - Turnaround time error (|sim_TAT - real_TAT| / sim_TAT)
 *    - Waiting time error
 *    - Response time error
 *    - Context switch delta
 *    - Jain's Fairness Index comparison
 *
 *  Usage:
 *    g++ -std=c++17 -O2 -Wall -o validator validator.cpp
 *    ./validator [--quantum <ms>]
 *
 *  Platform: Linux only (real backend uses fork/signals)
 *
 *  Design note on burst scaling:
 *    The simulator works in abstract "ticks" while the real backend uses
 *    milliseconds.  We treat 1 tick == 1 ms.  Bursts must be large enough
 *    (>= 50 ms recommended) for OS jitter to be negligible.
 */

#ifndef __linux__
#include <iostream>

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   validator: Linux-only component                            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Note: validator requires Linux kernel process controls (fork, SIGSTOP, SIGCONT).\n"
              << "To run validation against real processes:\n"
              << "  1. Use WSL (Windows Subsystem for Linux), Ubuntu, or a Linux VM.\n"
              << "  2. For cross-platform simulation and unit tests on Windows, run:\n"
              << "     ./scheduler_sim.exe\n"
              << "     ./run_tests.exe\n";
    return 0;
}

#else

#include "common.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <set>
#include <stdexcept>

#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

// ─── Bring in simulator algorithms (header-only inline versions) ──────────────
// We re-declare them here as free functions so this file is self-contained.
// (In a larger project these would be in separate .hpp/.cpp units.)

// Forward declarations (implemented later in this file to avoid duplicate
// symbol issues when linking with simulator.cpp / real_scheduler.cpp)
std::vector<Segment> sim_fcfs(const std::vector<Task>&);
std::vector<Segment> sim_rr  (const std::vector<Task>&, int);
std::vector<Segment> sim_mlfq(const std::vector<Task>&, int, int, int);
std::vector<Segment> sim_cfs (const std::vector<Task>&, double, double);

// ─── Real backend (inlined here to avoid duplicate main) ─────────────────────

static double now_ms_v() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms_v(double ms) {
    if (ms <= 0) return;
    struct timespec ts{static_cast<time_t>(ms/1000.0),
                       static_cast<long>((ms - static_cast<long>(ms/1000.0)*1000.0)*1e6)};
    nanosleep(&ts, nullptr);
}

static void xkill_v(pid_t pid, int sig) {
    if (kill(pid, sig) == -1 && errno != ESRCH) { std::perror("kill"); std::abort(); }
}

static void wait_for_stop_v(pid_t pid) {
    int st;
    while (true) {
        pid_t r = waitpid(pid, &st, WUNTRACED | WCONTINUED);
        if (r == -1 && errno == EINTR) continue;
        if (r == pid && WIFSTOPPED(st)) return;
    }
}

static volatile sig_atomic_t g_alarm_v = 0;
static void alarm_handler_v(int) { g_alarm_v = 1; }

static void arm_timer_v(double ms) {
    struct itimerval itv{};
    if (ms > 0) {
        long s = static_cast<long>(ms / 1000.0);
        long u = static_cast<long>((ms - s*1000.0) * 1000.0);
        itv.it_value = {s, u};
    }
    setitimer(ITIMER_REAL, &itv, nullptr);
}

[[noreturn]] static void child_workload(int burst_ms) {
    raise(SIGSTOP);
    struct timespec start, cur;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    volatile long long d = 0;
    while (true) {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cur);
        double el = (cur.tv_sec - start.tv_sec)*1000.0 + (cur.tv_nsec - start.tv_nsec)/1e6;
        if (el >= burst_ms) break;
        d++;
    }
    _exit(0);
}

struct RealProc {
    int    id, arrival_ms, burst_ms;
    pid_t  pid;
    double first_ms = -1, finish_ms = -1;
    bool   done     = false;
    double vruntime = 0;
    int    weight   = WEIGHT_NICE0;
    int    mlfq_level = 0, allot_rem = 0;
};

static std::vector<RealProc> spawn_v(const std::vector<Task>& tasks) {
    std::vector<RealProc> procs;
    for (const auto& t : tasks) {
        pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("fork failed");
        if (pid == 0) child_workload(t.burst);
        wait_for_stop_v(pid);
        RealProc p{}; p.id=t.id; p.pid=pid; p.arrival_ms=t.arrival;
        p.burst_ms=t.burst; p.weight=t.weight();
        procs.push_back(p);
    }
    return procs;
}

static double resume_v(RealProc& p, double q_ms) {
    if (p.first_ms < 0) p.first_ms = now_ms_v();
    g_alarm_v = 0;
    if (q_ms > 0) arm_timer_v(q_ms);
    xkill_v(p.pid, SIGCONT);
    return now_ms_v();
}

static double preempt_v(RealProc& p) {
    arm_timer_v(0);
    xkill_v(p.pid, SIGSTOP);
    wait_for_stop_v(p.pid);
    return now_ms_v();
}

// Wait with poll loop for exit or alarm
static bool wait_exit_or_alarm(pid_t pid) {
    int status;
    while (!g_alarm_v) {
        pid_t r = waitpid(pid, &status, WNOHANG | WUNTRACED);
        if (r == pid && (WIFEXITED(status) || WIFSIGNALED(status))) return true;
        struct timespec ns{0, 100000}; nanosleep(&ns, nullptr);
    }
    return false;
}

static std::vector<Segment> real_rr_v(std::vector<RealProc>& procs,
                                       double ss, double q_ms) {
    std::sort(procs.begin(), procs.end(), [](const RealProc& a, const RealProc& b){
        return a.arrival_ms < b.arrival_ms;
    });
    std::deque<int> ready;
    std::vector<bool> inq(procs.size(), false);
    std::vector<Segment> tl;
    double cur = ss; int idx = 0;
    auto admit = [&](double t){ while(idx<(int)procs.size()&&ss+procs[idx].arrival_ms<=t){
        if(!inq[idx]&&!procs[idx].done){ready.push_back(idx);inq[idx]=true;}idx++;}};
    admit(cur);
    if(ready.empty()&&idx<(int)procs.size()){cur=ss+procs[0].arrival_ms;admit(cur);}
    while(!ready.empty()){
        int i=ready.front();ready.pop_front();inq[i]=false;
        double s=resume_v(procs[i],q_ms);
        bool ex=wait_exit_or_alarm(procs[i].pid);
        double e; if(ex){arm_timer_v(0);e=now_ms_v();procs[i].finish_ms=e;procs[i].done=true;}
        else e=preempt_v(procs[i]);
        tl.push_back({procs[i].id,s-ss,e-ss});cur=e;
        admit(cur);
        if(!procs[i].done){ready.push_back(i);inq[i]=true;}
        if(ready.empty()&&idx<(int)procs.size()){cur=ss+procs[idx].arrival_ms;admit(cur);}
    }
    return tl;
}

static std::vector<TaskResult> real_results(const std::vector<RealProc>& procs, double ss) {
    std::vector<TaskResult> out;
    for(const auto& p:procs){
        TaskResult r{};r.id=p.id;r.arrival=p.arrival_ms;r.burst=p.burst_ms;
        r.start_time      = p.first_ms>=0 ? p.first_ms-ss : 0;
        r.completion_time = p.finish_ms>=0? p.finish_ms-ss: 0;
        r.turnaround=r.completion_time-p.arrival_ms;
        r.waiting=r.turnaround-p.burst_ms;
        r.response=r.start_time-p.arrival_ms;
        out.push_back(r);
    }
    std::sort(out.begin(),out.end(),[](const TaskResult&a,const TaskResult&b){return a.id<b.id;});
    return out;
}

// ============================================================================
//  Simulator algorithm implementations (local to this TU)
// ============================================================================

std::vector<Segment> sim_fcfs(const std::vector<Task>& tasks) {
    std::vector<Task> p=tasks;
    std::sort(p.begin(),p.end(),[](const Task&a,const Task&b){
        return a.arrival<b.arrival||(a.arrival==b.arrival&&a.id<b.id);});
    std::vector<Segment> tl; double cur=0;
    for(const auto&t:p){double s=std::max(cur,(double)t.arrival);tl.push_back({t.id,s,s+t.burst});cur=s+t.burst;}
    return tl;
}

std::vector<Segment> sim_rr(const std::vector<Task>& tasks, int q) {
    std::vector<Task> p=tasks;
    std::sort(p.begin(),p.end(),[](const Task&a,const Task&b){
        return a.arrival<b.arrival||(a.arrival==b.arrival&&a.id<b.id);});
    const int n=p.size();
    std::vector<int> rem(n); for(int i=0;i<n;i++) rem[i]=p[i].burst;
    std::deque<int> ready; std::vector<bool> inq(n,false);
    std::vector<Segment> tl; double cur=p[0].arrival; int idx=0;
    while(idx<n&&p[idx].arrival<=cur){ready.push_back(idx);inq[idx]=true;idx++;}
    while(!ready.empty()){
        int i=ready.front();ready.pop_front();inq[i]=false;
        int run=std::min(q,rem[i]);tl.push_back({p[i].id,cur,cur+run});cur+=run;rem[i]-=run;
        while(idx<n&&p[idx].arrival<=cur){if(!inq[idx]){ready.push_back(idx);inq[idx]=true;}idx++;}
        if(rem[i]>0){ready.push_back(i);inq[i]=true;}
        if(ready.empty()&&idx<n){cur=p[idx].arrival;
            while(idx<n&&p[idx].arrival<=cur){if(!inq[idx]){ready.push_back(idx);inq[idx]=true;}idx++;}}
    }
    return tl;
}

std::vector<Segment> sim_mlfq(const std::vector<Task>& tasks, int bq, int boost, int lv) {
    std::vector<Task> p=tasks;
    std::sort(p.begin(),p.end(),[](const Task&a,const Task&b){return a.arrival<b.arrival;});
    const int n=p.size();
    struct PS{int rem,level,allot,done;};
    std::vector<PS> st(n);
    auto qL=[&](int l)->int{return l>=lv-1?INF_TIME:bq*(1<<l);};
    for(int i=0;i<n;i++) st[i]={p[i].burst,0,qL(0),0};
    std::vector<std::deque<int>> qs(lv);
    std::vector<Segment> tl; double cur=p[0].arrival,lb=cur; int idx=0;
    auto admit=[&](double t){while(idx<n&&p[idx].arrival<=t){st[idx]={p[idx].burst,0,qL(0),0};qs[0].push_back(idx++);} };
    admit(cur);
    while(true){
        if(cur-lb>=boost){lb=cur;for(int l=1;l<lv;l++){for(int pi:qs[l]){st[pi].level=0;st[pi].allot=qL(0);qs[0].push_back(pi);}qs[l].clear();}}
        int cl=-1; for(int l=0;l<lv;l++) if(!qs[l].empty()){cl=l;break;}
        if(cl<0){if(idx<n){cur=p[idx].arrival;admit(cur);continue;}break;}
        int pi=qs[cl].front();qs[cl].pop_front();
        int run=std::min({st[pi].allot,st[pi].rem,(int)std::max(1.0,std::min((double)(idx<n?p[idx].arrival:INF_TIME)-cur,(double)(lb+boost-cur)))});
        if(run<=0) run=1;
        tl.push_back({p[pi].id,cur,cur+run,cl});cur+=run;st[pi].rem-=run;st[pi].allot-=run;
        admit(cur);
        if(st[pi].rem<=0){st[pi].done=1;}
        else if(st[pi].allot<=0){int nl=std::min(cl+1,lv-1);st[pi].level=nl;st[pi].allot=qL(nl);qs[nl].push_back(pi);}
        else{qs[cl].push_front(pi);}
    }
    return tl;
}

std::vector<Segment> sim_cfs(const std::vector<Task>& tasks, double tl_ms, double mg) {
    if (tasks.empty()) return {};
    struct RQE {
        double vrt;
        int    pi;
        bool operator<(const RQE& o) const {
            return vrt != o.vrt ? vrt < o.vrt : pi < o.pi;
        }
    };
    struct TS { int rem, w; double vrt; bool done; };
    const int n = static_cast<int>(tasks.size());
    std::vector<Task> st = tasks;
    std::sort(st.begin(), st.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });
    std::vector<TS> ts(n);
    for (int i = 0; i < n; i++) ts[i] = {st[i].burst, st[i].weight(), 0.0, false};
    std::set<RQE> rq;
    double mvrt = 0, cur = st[0].arrival;
    int idx = 0;
    auto tw = [&]{
        int s = 0;
        for (const auto& e : rq) s += ts[e.pi].w;
        return s == 0 ? 1 : s;
    };
    auto admit = [&](double t){
        while (idx < n && st[idx].arrival <= t) {
            double v = std::max(0.0, mvrt - tl_ms);
            ts[idx].vrt = v;
            rq.insert({v, idx});
            idx++;
        }
    };
    admit(cur);
    std::vector<Segment> tl;
    while (true) {
        if (rq.empty()) {
            if (idx < n) { cur = st[idx].arrival; admit(cur); continue; }
            break;
        }
        auto it = rq.begin();
        int pi = it->pi;
        int tot_w = tw(); // runnable total weight
        rq.erase(it);

        double s = (tl_ms * ts[pi].w) / tot_w;
        if (s < mg) s = mg;
        double na = (idx < n) ? static_cast<double>(st[idx].arrival) : static_cast<double>(INF_TIME);
        double act = std::min(s, static_cast<double>(ts[pi].rem));
        if (na < cur + act) act = na - cur;
        if (act <= 0) {
            admit(cur);
            rq.insert({ts[pi].vrt, pi});
            continue;
        }
        tl.push_back({st[pi].id, cur, cur + act});
        ts[pi].vrt += (act * WEIGHT_NICE0) / ts[pi].w;
        ts[pi].rem -= static_cast<int>(act);
        cur += act;
        if (!rq.empty()) {
            mvrt = std::max(mvrt, rq.begin()->vrt);
        }
        mvrt = std::max(mvrt, ts[pi].vrt - tl_ms);
        admit(cur);
        if (ts[pi].rem > 0) {
            rq.insert({ts[pi].vrt, pi});
        } else {
            ts[pi].done = true;
        }
    }
    return tl;
}

// ============================================================================
//  Validation report
// ============================================================================

struct ValidationRow {
    std::string metric;
    double      sim_val;
    double      real_val;
    double      abs_error;
    double      pct_error; // |sim-real|/sim * 100
};

static void printValidation(const std::string&             algo,
                             const std::vector<TaskResult>& sim_res,
                             const std::vector<TaskResult>& real_res,
                             const RunSummary&              sim_sum,
                             const RunSummary&              real_sum)
{
    const std::string sep(72, '=');
    std::cout << '\n' << sep << '\n';
    std::cout << "  VALIDATION REPORT — " << algo << '\n';
    std::cout << sep << '\n';

    auto pct = [](double sim, double real) -> double {
        if (sim == 0) return 0;
        return std::abs(sim - real) / sim * 100.0;
    };

    std::vector<ValidationRow> rows = {
        {"Avg TAT",       sim_sum.avg_turnaround,  real_sum.avg_turnaround,
         std::abs(sim_sum.avg_turnaround-real_sum.avg_turnaround),
         pct(sim_sum.avg_turnaround, real_sum.avg_turnaround)},
        {"Avg WT",        sim_sum.avg_waiting,     real_sum.avg_waiting,
         std::abs(sim_sum.avg_waiting-real_sum.avg_waiting),
         pct(sim_sum.avg_waiting, real_sum.avg_waiting)},
        {"Avg RT",        sim_sum.avg_response,    real_sum.avg_response,
         std::abs(sim_sum.avg_response-real_sum.avg_response),
         pct(sim_sum.avg_response, real_sum.avg_response)},
        {"Ctx Switches",  (double)sim_sum.context_switches, (double)real_sum.context_switches,
         std::abs((double)sim_sum.context_switches - real_sum.context_switches), 0.0},
        {"CPU Util%",     sim_sum.cpu_utilization, real_sum.cpu_utilization,
         std::abs(sim_sum.cpu_utilization - real_sum.cpu_utilization),
         pct(sim_sum.cpu_utilization, real_sum.cpu_utilization)},
        {"Jain's J",      sim_sum.jains_fairness,  real_sum.jains_fairness,
         std::abs(sim_sum.jains_fairness - real_sum.jains_fairness),
         pct(sim_sum.jains_fairness, real_sum.jains_fairness)},
    };

    constexpr int MW = 16, W = 12;
    std::cout << std::left << std::setw(MW) << "Metric"
              << std::setw(W) << "Simulated"
              << std::setw(W) << "Real"
              << std::setw(W) << "Abs Error"
              << "% Error\n";
    std::cout << std::string(MW + 3*W + 10, '-') << '\n';
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& r : rows) {
        std::cout << std::left << std::setw(MW) << r.metric
                  << std::setw(W) << r.sim_val
                  << std::setw(W) << r.real_val
                  << std::setw(W) << r.abs_error
                  << r.pct_error << "%\n";
    }
    std::cout << sep << '\n';

    // Per-task turnaround comparison
    std::cout << "\nPer-task TAT comparison:\n";
    std::cout << std::setw(6) << "PID" << std::setw(12) << "Sim TAT"
              << std::setw(12) << "Real TAT" << std::setw(12) << "Delta" << '\n';
    std::cout << std::string(42, '-') << '\n';
    for (size_t i = 0; i < std::min(sim_res.size(), real_res.size()); i++) {
        double d = real_res[i].turnaround - sim_res[i].turnaround;
        std::cout << std::setw(6) << sim_res[i].id
                  << std::setw(12) << sim_res[i].turnaround
                  << std::setw(12) << real_res[i].turnaround
                  << std::setw(12) << d << '\n';
    }
    std::cout << sep << '\n';
}

// ─── Input ────────────────────────────────────────────────────────────────────

static std::vector<Task> readTasks_v() {
    int n;
    std::cout << "Number of processes: ";
    std::cin >> n;
    if (n <= 0) throw std::invalid_argument("n > 0 required");
    std::vector<Task> tasks(n);
    std::cout << "Enter (arrival_ms  burst_ms  nice[-20..19]) for each process:\n";
    for (int i = 0; i < n; i++) {
        tasks[i].id = i;
        std::cout << "  P" << i << ": ";
        std::cin >> tasks[i].arrival >> tasks[i].burst >> tasks[i].nice;
        if (tasks[i].burst <= 0) throw std::invalid_argument("burst > 0 required");
    }
    return tasks;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    struct sigaction sa{};
    sa.sa_handler = alarm_handler_v;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, nullptr);

    double quantum_ms = 100.0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--quantum" && i+1 < argc) quantum_ms = std::stod(argv[++i]);
    }
    int quantum_ticks = static_cast<int>(quantum_ms);

    std::cout << "╔══════════════════════════════════════════════════╗\n";
    std::cout << "║   Simulator ↔ Real-Backend Validator             ║\n";
    std::cout << "║   Compares predicted vs measured metrics         ║\n";
    std::cout << "╚══════════════════════════════════════════════════╝\n\n";
    std::cout << "NOTE: Use burst_ms >= 50 for meaningful real-mode results.\n\n";

    try {
        auto tasks = readTasks_v();

        // ── Round Robin validation ──
        {
            std::cout << "\n--- Simulator: RR(q=" << quantum_ticks << ") ---\n";
            auto sim_tl  = sim_rr(tasks, quantum_ticks);
            auto sim_res = buildResults(tasks, sim_tl);
            auto sim_sum = buildSummary("Sim-RR", sim_tl, sim_res);
            printResultsTable(sim_res);
            printGantt(sim_tl);

            std::cout << "\n--- Real: RR(q=" << static_cast<int>(quantum_ms) << "ms) ---\n";
            auto procs    = spawn_v(tasks);
            double ss     = now_ms_v();
            auto real_tl  = real_rr_v(procs, ss, quantum_ms);
            auto real_res = real_results(procs, ss);
            auto real_sum = buildSummary("Real-RR", real_tl, real_res);
            // Cleanup
            for (auto& p : procs) if (!p.done) { xkill_v(p.pid, SIGKILL); int st; waitpid(p.pid,&st,0); }
            printResultsTable(real_res, true);
            printGantt(real_tl, true);

            printValidation("Round Robin", sim_res, real_res, sim_sum, real_sum);
        }

        // ── FCFS validation ──
        {
            std::cout << "\n--- Simulator: FCFS ---\n";
            auto sim_tl  = sim_fcfs(tasks);
            auto sim_res = buildResults(tasks, sim_tl);
            auto sim_sum = buildSummary("Sim-FCFS", sim_tl, sim_res);
            printResultsTable(sim_res);
            printGantt(sim_tl);

            std::cout << "\n--- Real: FCFS ---\n";
            auto procs = spawn_v(tasks);
            std::sort(procs.begin(), procs.end(), [](const RealProc& a, const RealProc& b){
                return a.arrival_ms < b.arrival_ms;
            });
            double ss = now_ms_v();
            std::vector<Segment> real_tl;
            double cur = ss;
            for (auto& p : procs) {
                double arr_abs = ss + p.arrival_ms;
                if (cur < arr_abs) { sleep_ms_v(arr_abs - cur); cur = now_ms_v(); }
                double seg_s = resume_v(p, 0);
                // Wait for exit (FCFS: no preemption)
                int status;
                while (true) {
                    pid_t r = waitpid(p.pid, &status, WUNTRACED);
                    if (r == -1 && errno == EINTR) continue;
                    if (WIFEXITED(status) || WIFSIGNALED(status)) break;
                    xkill_v(p.pid, SIGCONT);
                }
                p.finish_ms = now_ms_v(); p.done = true;
                real_tl.push_back({p.id, seg_s - ss, p.finish_ms - ss});
                cur = p.finish_ms;
            }
            auto real_res = real_results(procs, ss);
            auto real_sum = buildSummary("Real-FCFS", real_tl, real_res);
            printResultsTable(real_res, true);
            printGantt(real_tl, true);

            printValidation("FCFS", sim_res, real_res, sim_sum, real_sum);
        }

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif // __linux__
