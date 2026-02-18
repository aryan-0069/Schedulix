# ─── OS Scheduler Project — Makefile ─────────────────────────────────────────
#
#  Targets:
#    make all          — build all binaries
#    make simulator    — pure simulation engine
#    make real         — real Linux process scheduler (Linux only)
#    make validator    — sim ↔ real validator (Linux only)
#    make tests        — unit + integration test binary
#    make run-sim      — build + run simulator with sample workload
#    make run-tests    — build + run tests
#    make clean        — remove build artefacts

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I.
LDFLAGS  :=

# Detect Linux
UNAME := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME),Linux)
  HAS_LINUX := 1
else
  HAS_LINUX := 0
endif

# ─── Sources ──────────────────────────────────────────────────────────────────

SIM_SRC   := simulator.cpp
REAL_SRC  := real_scheduler.cpp
VAL_SRC   := validator.cpp
TEST_SRC  := tests.cpp
COMMON_H  := common.hpp

# ─── Binaries ────────────────────────────────────────────────────────────────

SIM_BIN   := scheduler_sim
REAL_BIN  := scheduler_real
VAL_BIN   := validator
TEST_BIN  := run_tests

# ─── Default target ──────────────────────────────────────────────────────────

.PHONY: all
all: $(SIM_BIN) $(TEST_BIN)
ifeq ($(HAS_LINUX),1)
all: $(SIM_BIN) $(REAL_BIN) $(VAL_BIN) $(TEST_BIN)
endif

# ─── Build rules ─────────────────────────────────────────────────────────────

$(SIM_BIN): $(SIM_SRC) $(COMMON_H)
	$(CXX) $(CXXFLAGS) -o $@ $(SIM_SRC) $(LDFLAGS)
	@echo "Built: $(SIM_BIN)"

$(REAL_BIN): $(REAL_SRC) $(COMMON_H)
ifeq ($(HAS_LINUX),1)
	$(CXX) $(CXXFLAGS) -o $@ $(REAL_SRC) $(LDFLAGS)
	@echo "Built: $(REAL_BIN)"
else
	@echo "Skipping $(REAL_BIN): requires Linux"
endif

$(VAL_BIN): $(VAL_SRC) $(COMMON_H)
ifeq ($(HAS_LINUX),1)
	$(CXX) $(CXXFLAGS) -o $@ $(VAL_SRC) $(LDFLAGS)
	@echo "Built: $(VAL_BIN)"
else
	@echo "Skipping $(VAL_BIN): requires Linux"
endif

$(TEST_BIN): $(TEST_SRC) $(COMMON_H)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SRC) $(LDFLAGS)
	@echo "Built: $(TEST_BIN)"

# ─── Convenience aliases ─────────────────────────────────────────────────────

.PHONY: simulator real validator tests
simulator: $(SIM_BIN)
real:      $(REAL_BIN)
validator: $(VAL_BIN)
tests:     $(TEST_BIN)

# ─── Run targets ─────────────────────────────────────────────────────────────

.PHONY: run-tests
run-tests: $(TEST_BIN)
	./$(TEST_BIN)

.PHONY: run-sim
run-sim: $(SIM_BIN)
	@echo "Running simulator with sample 4-process workload..."
	@echo "4\n0 6 0\n0 4 0\n0 2 0\n0 8 0\n2" | ./$(SIM_BIN)

.PHONY: run-real
run-real: $(REAL_BIN)
ifeq ($(HAS_LINUX),1)
	@echo "Running real scheduler with sample workload (100ms bursts)..."
	@echo "3\n0 200 0\n0 300 0\n0 150 0\n100" | ./$(REAL_BIN) --rr --quantum 100
else
	@echo "run-real: Linux only"
endif

.PHONY: run-validator
run-validator: $(VAL_BIN)
ifeq ($(HAS_LINUX),1)
	@echo "Running validator — comparing sim vs real..."
	@echo "3\n0 200 0\n50 150 0\n100 300 0" | ./$(VAL_BIN) --quantum 100
else
	@echo "run-validator: Linux only"
endif

# ─── Debug build ─────────────────────────────────────────────────────────────

.PHONY: debug
debug: CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra -I.
debug: $(SIM_BIN) $(TEST_BIN)
ifeq ($(HAS_LINUX),1)
debug: $(REAL_BIN) $(VAL_BIN)
endif

# ─── Clean ────────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	rm -f $(SIM_BIN) $(REAL_BIN) $(VAL_BIN) $(TEST_BIN) *.exe
	rm -f *.o *.d
	@echo "Cleaned."

# ─── Help ────────────────────────────────────────────────────────────────────

.PHONY: help
help:
	@echo ""
	@echo "OS Scheduler Project — Build Targets"
	@echo "────────────────────────────────────────────────────────"
	@echo "  make all           Build all binaries"
	@echo "  make simulator     Simulation engine only (cross-platform)"
	@echo "  make real          Real process scheduler (Linux only)"
	@echo "  make validator     Sim↔real validator (Linux only)"
	@echo "  make tests         Unit and integration tests"
	@echo "  make run-tests     Build and run tests"
	@echo "  make run-sim       Build and run simulator with sample data"
	@echo "  make run-real      Run real scheduler with sample data"
	@echo "  make run-validator Run validator with sample data"
	@echo "  make debug         Debug build (-O0 -g)"
	@echo "  make clean         Remove build artefacts"
	@echo ""
	@echo "  Compiler: $(CXX)   Flags: $(CXXFLAGS)"
	@echo ""
