# ── market-data-pipeline ──────────────────────────────────────────────────────
#   make            build everything (Release: -O3 -march=native)
#   make test       build + run unit tests
#   make run        build + run the pipeline (2M messages)
#   make bench      build + run the benchmark suite
#   make tsan       build + run the queue tests under ThreadSanitizer
#   make clean      remove all build output

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O3 -march=native -Wall -Wextra -Wpedantic -Wshadow -pthread
CPPFLAGS  = -Iinclude

BUILD := build

# Objects shared by the pipeline and benchmark executables.
COMMON_SRC := src/cpu_affinity.cpp src/feed_generator.cpp src/feed_handler.cpp src/strategy.cpp
COMMON_OBJ := $(COMMON_SRC:src/%.cpp=$(BUILD)/%.o)

BINARIES := $(BUILD)/pipeline $(BUILD)/benchmark $(BUILD)/test_spsc $(BUILD)/test_pipeline_zero

all: $(BINARIES)

# Compile rule: src/foo.cpp → build/foo.o
# -MMD -MP writes a .d file per object listing the headers it includes, so
# editing a header rebuilds exactly the objects that depend on it.
$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

# ── Executables ───────────────────────────────────────────────────────────────
$(BUILD)/pipeline: $(COMMON_OBJ) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/benchmark: $(COMMON_OBJ) $(BUILD)/benchmark.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Queue test is header-only — compiles directly from the test source.
$(BUILD)/test_spsc: tests/test_spsc_queue.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD)/test_pipeline_zero: tests/test_pipeline_zero.cpp \
		$(BUILD)/cpu_affinity.o $(BUILD)/feed_generator.o $(BUILD)/feed_handler.o | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD):
	mkdir -p $(BUILD)

# ── Convenience targets ───────────────────────────────────────────────────────
test: $(BUILD)/test_spsc $(BUILD)/test_pipeline_zero
	./$(BUILD)/test_spsc
	./$(BUILD)/test_pipeline_zero

run: $(BUILD)/pipeline
	./$(BUILD)/pipeline

bench: $(BUILD)/benchmark
	./$(BUILD)/benchmark

# ThreadSanitizer build of the concurrency tests (Linux; catches data races).
tsan: | $(BUILD)
	$(CXX) $(CPPFLAGS) -std=c++20 -O1 -g -fsanitize=thread -pthread \
		tests/test_spsc_queue.cpp -o $(BUILD)/test_spsc_tsan
	./$(BUILD)/test_spsc_tsan

clean:
	rm -rf $(BUILD)

# Pull in the auto-generated header dependency files (if any exist yet).
-include $(BUILD)/*.d

.PHONY: all test run bench tsan clean
