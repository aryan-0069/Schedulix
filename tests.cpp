/**
 * @file   tests.cpp
 * @brief  Unit and integration tests for the OS Scheduler project.
 *
 *  Tests are organised into suites:
 *    1. Simulator correctness (textbook worked examples with known answers)
 *    2. MLFQ rule invariants (demotion, boost, starvation prevention)
 *    3. CFS fairness (Jain's Index, proportional allocation)
 *    4. Common utilities (Gantt, buildResults, Jain's)
 *
 *  No external test framework required — plain assertions with descriptive
 *  failure messages.  Each test prints PASS or FAIL.
 *
 *  Compile:
 *    g++ -std=c++17 -O2 -Wall -o tests tests.cpp
 *  Run:
 *    ./tests
 */

#include "common.hpp"

#include <cassert>
#include <deque>
#include <functional>
#include <iomanip>
#include <queue>
#include <set>
#include <stdexcept>
#include <cmath>

// ─── Re-implement slim versions of scheduler algorithms (no I/O) ───────────────
// These are exact copies of the algorithms in simulator.cpp, renamed to avoid
// linker conflicts when tests.cpp is compiled standalone.

static std::vector<Segment> t_fcfs(const std::vector<Task>& tasks) {
    std::vector<Task> p = tasks;
    std::sort(p.begin(), p.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });
    std::vector<Segment> tl; double cur = 0;
    for (const auto& t : p) {
        double s = std::max(cur, (double)t.arrival);
        tl.push_back({t.id, s, s + t.burst});
        cur = s + t.burst;
    }
    return tl;
}

static std::vector<Segment> t_sjf(const std::vector<Task>& tasks) {
    std::vector<Task> p = tasks;
    std::sort(p.begin(), p.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival;
    });
    using T3 = std::tuple<int,int,int>;
    std::priority_queue<T3, std::vector<T3>, std::greater<T3>> rq;
    std::vector<Segment> tl;
    double cur = p[0].arrival; int idx = 0;
    while (idx < (int)p.size() || !rq.empty()) {
        while (idx < (int)p.size() && p[idx].arrival <= cur) {
            rq.push({p[idx].burst, p[idx].arrival, p[idx].id}); idx++;
        }
        if (rq.empty()) { cur = p[idx].arrival; continue; }
        auto [bt, at, id] = rq.top(); rq.pop();
        tl.push_back({id, cur, cur + bt}); cur += bt;
    }
    return tl;
}

static std::vector<Segment> t_srtf(const std::vector<Task>& tasks) {
    std::vector<Task> p = tasks;
    std::sort(p.begin(), p.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival;
    });
    const int n = p.size();
    std::vector<int> rem(n); for (int i=0;i<n;i++) rem[i]=p[i].burst;
    using P2=std::pair<int,int>;
    std::priority_queue<P2,std::vector<P2>,std::greater<P2>> rq;
    std::vector<Segment> tl;
    double cur = p[0].arrival; int idx = 0;
    auto push_r = [&](double t){ while(idx<n&&(double)p[idx].arrival<=t){rq.push({rem[idx],p[idx].id});idx++;}};
    push_r(cur);
    while (idx < n || !rq.empty()) {
        push_r(cur); if(rq.empty()){cur=(double)p[idx].arrival;continue;}
        double na=(idx<n)?(double)p[idx].arrival:(double)INF_TIME;
        auto [rm,id]=rq.top();rq.pop();
        double run=std::min((double)rm,na-cur); if(run<=0)run=(double)rm;
        tl.push_back({id, cur, cur + run}); cur += run;
        for(int i=0;i<n;i++) if(p[i].id==id){rem[i]-=(int)run;if(rem[i]>0)rq.push({rem[i],id});break;}
    }
    return tl;
}

static std::vector<Segment> t_rr(const std::vector<Task>& tasks, int q) {
    std::vector<Task> p = tasks;
    std::sort(p.begin(), p.end(), [](const Task& a, const Task& b){
        return a.arrival < b.arrival || (a.arrival == b.arrival && a.id < b.id);
    });
    const int n = p.size();
    std::vector<int> rem(n); for(int i=0;i<n;i++) rem[i]=p[i].burst;
    std::deque<int> ready; std::vector<bool> inq(n,false);
    std::vector<Segment> tl; double cur=p[0].arrival; int idx=0;
    while(idx<n&&p[idx].arrival<=cur){ready.push_back(idx);inq[idx]=true;idx++;}
    while(!ready.empty()){
        int i=ready.front();ready.pop_front();inq[i]=false;
        int run=std::min(q,rem[i]);
        tl.push_back({p[i].id, cur, cur + run}); cur += run; rem[i] -= run;
        while(idx<n&&p[idx].arrival<=cur){if(!inq[idx]){ready.push_back(idx);inq[idx]=true;}idx++;}
        if(rem[i]>0){ready.push_back(i);inq[i]=true;}
        if(ready.empty()&&idx<n){cur=p[idx].arrival;
            while(idx<n&&p[idx].arrival<=cur){if(!inq[idx]){ready.push_back(idx);inq[idx]=true;}idx++;}}
    }
    return tl;
}

static std::vector<Segment> t_mlfq(const std::vector<Task>& tasks, int bq=4, int boost=100, int lv=3) {
    std::vector<Task> p=tasks;
    std::sort(p.begin(),p.end(),[](const Task&a,const Task&b){return a.arrival<b.arrival;});
    const int n=p.size();
    struct PS{int rem,level,allot;};
    auto qL=[&](int l)->int{return l>=lv-1?INF_TIME:bq*(1<<l);};
    std::vector<PS> st(n);for(int i=0;i<n;i++)st[i]={p[i].burst,0,qL(0)};
    std::vector<std::deque<int>> qs(lv);
    std::vector<Segment> tl;double cur=p[0].arrival,lb=cur;int idx=0;
    auto admit=[&](double t){while(idx<n&&p[idx].arrival<=t){st[idx]={p[idx].burst,0,qL(0)};qs[0].push_back(idx++);}};
    admit(cur);
    while(true){
        if(cur-lb>=boost){lb=cur;for(int l=1;l<lv;l++){for(int pi:qs[l]){st[pi]={st[pi].rem,0,qL(0)};qs[0].push_back(pi);}qs[l].clear();}}
        int cl=-1;for(int l=0;l<lv;l++)if(!qs[l].empty()){cl=l;break;}
        if(cl<0){if(idx<n){cur=p[idx].arrival;admit(cur);continue;}break;}
        int pi=qs[cl].front();qs[cl].pop_front();
        int na=(idx<n)?(int)p[idx].arrival:INF_TIME;
        int boost_end=(int)(lb+boost);
        int run=std::min({st[pi].allot,st[pi].rem,std::max(1,std::min(na-(int)cur,boost_end-(int)cur))});
        tl.push_back({p[pi].id, cur, cur + run, cl}); cur += run; st[pi].rem -= run; st[pi].allot -= run;
        admit(cur);
        if(st[pi].rem<=0){}
        else if(st[pi].allot<=0){int nl=std::min(cl+1,lv-1);st[pi].level=nl;st[pi].allot=qL(nl);qs[nl].push_back(pi);}
        else{qs[cl].push_front(pi);}
    }
    return tl;
}

// ─── Test infrastructure ──────────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0;

static void check(const std::string& name, bool cond, const std::string& detail = "") {
    if (cond) {
        std::cout << "[PASS] " << name << '\n';
        g_pass++;
    } else {
        std::cout << "[FAIL] " << name;
        if (!detail.empty()) std::cout << " — " << detail;
        std::cout << '\n';
        g_fail++;
    }
}

static void check_near(const std::string& name, double got, double expected, double tol = 0.5) {
    bool ok = std::abs(got - expected) <= tol;
    if (!ok) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2)
           << "got=" << got << " expected=" << expected << " tol=" << tol;
        check(name, false, ss.str());
    } else {
        check(name, true);
    }
}

// ─── Helper: get result for a single task id ──────────────────────────────────

static TaskResult getResult(const std::vector<TaskResult>& res, int id) {
    for (const auto& r : res) if (r.id == id) return r;
    throw std::runtime_error("task id not found: " + std::to_string(id));
}

// ─── Suite 1: FCFS correctness ────────────────────────────────────────────────
//
// Textbook example:
//   P0: AT=0, BT=4   → ST=0,  CT=4,  TAT=4,  WT=0
//   P1: AT=1, BT=3   → ST=4,  CT=7,  TAT=6,  WT=3
//   P2: AT=2, BT=5   → ST=7,  CT=12, TAT=10, WT=5
//   Avg TAT=6.67, Avg WT=2.67

static void suite_fcfs() {
    std::cout << "\n=== Suite 1: FCFS ===\n";
    std::vector<Task> tasks = {{0,0,4},{1,1,3},{2,2,5}};
    auto tl = t_fcfs(tasks);
    auto res = buildResults(tasks, tl);

    auto r0 = getResult(res, 0); auto r1 = getResult(res, 1); auto r2 = getResult(res, 2);

    check_near("FCFS P0 start_time",      r0.start_time,      0.0);
    check_near("FCFS P0 completion_time", r0.completion_time, 4.0);
    check_near("FCFS P0 turnaround",      r0.turnaround,      4.0);
    check_near("FCFS P0 waiting",         r0.waiting,         0.0);

    check_near("FCFS P1 start_time",      r1.start_time,      4.0);
    check_near("FCFS P1 completion_time", r1.completion_time, 7.0);
    check_near("FCFS P1 turnaround",      r1.turnaround,      6.0);
    check_near("FCFS P1 waiting",         r1.waiting,         3.0);

    check_near("FCFS P2 start_time",      r2.start_time,      7.0);
    check_near("FCFS P2 completion_time", r2.completion_time, 12.0);
    check_near("FCFS P2 turnaround",      r2.turnaround,      10.0);
    check_near("FCFS P2 waiting",         r2.waiting,         5.0);

    // Avg checks
    double avg_tat = (r0.turnaround + r1.turnaround + r2.turnaround) / 3.0;
    double avg_wt  = (r0.waiting    + r1.waiting    + r2.waiting   ) / 3.0;
    check_near("FCFS Avg TAT", avg_tat, 20.0/3.0, 0.01);
    check_near("FCFS Avg WT",  avg_wt,  8.0/3.0,  0.01);

    // CPU utilization — all processes run without gaps → 100%
    auto s = buildSummary("FCFS", tl, res);
    check_near("FCFS CPU utilization", s.cpu_utilization, 100.0, 0.1);

    // Context switches: FCFS never preempts → 2 switches (P0→P1, P1→P2)
    check("FCFS context switches", s.context_switches == 2);
}

// ─── Suite 2: SJF correctness ─────────────────────────────────────────────────
//
// Textbook example:
//   P0: AT=0, BT=6
//   P1: AT=1, BT=2
//   P2: AT=2, BT=8
//   P3: AT=3, BT=3
//
//   At t=0: only P0 available → P0 runs [0,6]
//   At t=6: P1(2), P3(3), P2(8) available → pick P1 [6,8]
//   At t=8: P3(3), P2(8) → pick P3 [8,11]
//   At t=11: P2(8) → P2 [11,19]
//
//   P0: ST=0,  CT=6,  TAT=6,  WT=0
//   P1: ST=6,  CT=8,  TAT=7,  WT=5
//   P2: ST=11, CT=19, TAT=17, WT=9
//   P3: ST=8,  CT=11, TAT=8,  WT=5

static void suite_sjf() {
    std::cout << "\n=== Suite 2: SJF (non-preemptive) ===\n";
    std::vector<Task> tasks = {{0,0,6},{1,1,2},{2,2,8},{3,3,3}};
    auto tl = t_sjf(tasks);
    auto res = buildResults(tasks, tl);

    auto r0=getResult(res,0); auto r1=getResult(res,1);
    auto r2=getResult(res,2); auto r3=getResult(res,3);

    check_near("SJF P0 CT",  r0.completion_time, 6.0);
    check_near("SJF P1 CT",  r1.completion_time, 8.0);
    check_near("SJF P3 CT",  r3.completion_time, 11.0);
    check_near("SJF P2 CT",  r2.completion_time, 19.0);
    check_near("SJF P1 WT",  r1.waiting,         5.0);
    check_near("SJF P3 WT",  r3.waiting,         5.0);
    check_near("SJF P0 WT",  r0.waiting,         0.0);
}

// ─── Suite 3: SRTF correctness ────────────────────────────────────────────────
//
// Classic SRTF example:
//   P0: AT=0, BT=8
//   P1: AT=1, BT=4
//   P2: AT=2, BT=9
//   P3: AT=3, BT=5
//
//   t=0: P0 starts [0, 1)
//   t=1: P1 arrives rem(P1)=4 < rem(P0)=7 → preempt P0, P1 runs [1, 2)
//   t=2: P2 arrives rem(P2)=9 > rem(P1)=3 → P1 continues
//   t=3: P3 arrives rem(P3)=5 > rem(P1)=2 → P1 continues [1, 5]
//   t=5: P1 done. Now P0(7), P3(5), P2(9). Pick P3 [5,10]
//   t=10: P3 done. P0(7), P2(9). Pick P0 [10,17]
//   t=17: P0 done. P2(9) [17,26]
//
//   P0: CT=17, TAT=17, WT=9
//   P1: CT=5,  TAT=4,  WT=0
//   P2: CT=26, TAT=24, WT=15
//   P3: CT=10, TAT=7,  WT=2

static void suite_srtf() {
    std::cout << "\n=== Suite 3: SRTF ===\n";
    std::vector<Task> tasks = {{0,0,8},{1,1,4},{2,2,9},{3,3,5}};
    auto tl  = t_srtf(tasks);
    auto res = buildResults(tasks, tl);

    auto r0=getResult(res,0); auto r1=getResult(res,1);
    auto r2=getResult(res,2); auto r3=getResult(res,3);

    check_near("SRTF P1 CT",  r1.completion_time, 5.0);
    check_near("SRTF P3 CT",  r3.completion_time, 10.0);
    check_near("SRTF P0 CT",  r0.completion_time, 17.0);
    check_near("SRTF P2 CT",  r2.completion_time, 26.0);
    check_near("SRTF P0 WT",  r0.waiting,         9.0);
    check_near("SRTF P1 WT",  r1.waiting,         0.0);
    check_near("SRTF P3 WT",  r3.waiting,         2.0);
}

