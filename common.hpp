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

