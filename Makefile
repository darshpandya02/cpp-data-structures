# Header-only library: the targets below build the tests, the benchmarks and
# the demo program. Everything lands in build/.
#
#   make test        unit tests (debug, -O1)
#   make asan        tests under AddressSanitizer + UndefinedBehaviorSanitizer
#   make tsan        tests under ThreadSanitizer
#   make leaks       macOS only: tests under the `leaks` tool
#   make bench       benchmarks against the standard containers (-O2)
#   make demo        build/dsl-demo
#   make check       test + asan + tsan

CXX      ?= c++
STD      := -std=c++20
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
INCLUDES := -Iinclude -Ithird_party/doctest
CXXFLAGS ?=
ifeq ($(WERROR),1)
  WARN += -Werror
endif

BUILD    := build
HEADERS  := $(wildcard include/dsl/*.hpp)
TEST_SRC := $(wildcard tests/*.cpp)
TEST_DEPS := $(HEADERS) tests/support.hpp

SAN_COMMON := -g -O1 -fno-omit-frame-pointer -fno-sanitize-recover=all

.PHONY: all test asan tsan leaks bench demo check clean

all: $(BUILD)/tests $(BUILD)/bench $(BUILD)/dsl-demo

# One object per test file so they compile in parallel (make -j).
define test_variant
$(BUILD)/$(1)/%.o: tests/%.cpp $(TEST_DEPS) | $(BUILD)/$(1)
	$$(CXX) $(STD) $(WARN) $(INCLUDES) $(2) $$(CXXFLAGS) -c $$< -o $$@
$(BUILD)/$(1)/tests: $(patsubst tests/%.cpp,$(BUILD)/$(1)/%.o,$(TEST_SRC))
	$$(CXX) $(2) $$(CXXFLAGS) $$^ -o $$@ -pthread
$(BUILD)/$(1):
	@mkdir -p $$@
endef

$(eval $(call test_variant,debug,-g -O1))
$(eval $(call test_variant,asan,$(SAN_COMMON) -fsanitize=address,undefined))
$(eval $(call test_variant,tsan,$(SAN_COMMON) -fsanitize=thread))

$(BUILD)/tests: $(BUILD)/debug/tests
	cp $< $@

test: $(BUILD)/tests
	./$(BUILD)/tests

asan: $(BUILD)/asan/tests
	ASAN_OPTIONS=$${ASAN_OPTIONS:-detect_stack_use_after_return=1:strict_init_order=1} \
	UBSAN_OPTIONS=print_stacktrace=1 ./$(BUILD)/asan/tests

tsan: $(BUILD)/tsan/tests
	TSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/tsan/tests

leaks: $(BUILD)/tests
	MallocStackLogging=1 leaks --atExit -- ./$(BUILD)/tests --no-intro

check: test asan tsan

$(BUILD)/bench: bench/bench.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(STD) $(WARN) $(INCLUDES) -O2 -DNDEBUG $(CXXFLAGS) $< -o $@

bench: $(BUILD)/bench
	./$(BUILD)/bench

$(BUILD)/dsl-demo: examples/dsl_demo.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(STD) $(WARN) $(INCLUDES) -O2 $(CXXFLAGS) $< -o $@

demo: $(BUILD)/dsl-demo

clean:
	rm -rf $(BUILD)
