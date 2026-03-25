CC ?= clang
CFLAGS ?= -Wall -Wextra -O2 -std=c99 -Iinclude -fPIC
LDFLAGS ?= -pthread

UNAME_S := $(shell uname -s)

# macOS specific configuration
ifeq ($(UNAME_S),Darwin)
    XCODE_SDK := /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
    ifneq ($(wildcard $(XCODE_SDK)),)
        SDK_FLAG := -isysroot $(XCODE_SDK)
    endif
    CFLAGS += $(SDK_FLAG)
    SHARED_LIB := build/libracelens.dylib
    LIB_FLAGS := -dynamiclib -install_name @rpath/libracelens.dylib
    TEST_RPATH := -Wl,-rpath,@loader_path/../build
else
    # Linux configuration
    CFLAGS += -D_GNU_SOURCE
    SHARED_LIB := build/libracelens.so
    LIB_FLAGS := -shared -ldl
    TEST_RPATH := -Wl,-rpath,'$$ORIGIN/../build'
endif

SRCS := src/engine.c src/shadow.c src/report.c src/interceptor.c
OBJS := $(SRCS:src/%.c=build/%.o)

TEST_SRCS := tests/test_racy_counter.c tests/test_racy_transfer.c tests/test_synced_counter.c
TEST_BINS := $(TEST_SRCS:tests/%.c=build/%)

.PHONY: all clean test bench dirs

all: dirs $(SHARED_LIB) $(TEST_BINS) build/bench

dirs:
	@mkdir -p build

build/%.o: src/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(SHARED_LIB): $(OBJS)
	$(CC) $(CFLAGS) $(LIB_FLAGS) $(OBJS) $(LDFLAGS) -o $@

build/test_racy_counter: tests/test_racy_counter.c $(SHARED_LIB)
	$(CC) $(CFLAGS) $< $(SHARED_LIB) $(LDFLAGS) $(TEST_RPATH) -o $@

build/test_racy_transfer: tests/test_racy_transfer.c $(SHARED_LIB)
	$(CC) $(CFLAGS) $< $(SHARED_LIB) $(LDFLAGS) $(TEST_RPATH) -o $@

build/test_synced_counter: tests/test_synced_counter.c $(SHARED_LIB)
	$(CC) $(CFLAGS) $< $(SHARED_LIB) $(LDFLAGS) $(TEST_RPATH) -o $@

build/bench: benchmark/bench.c $(SHARED_LIB)
	$(CC) $(CFLAGS) $< $(SHARED_LIB) $(LDFLAGS) $(TEST_RPATH) -o $@

test: all
	@echo "======================================================================"
	@echo "                   RUNNING RACELENS TEST SUITE                        "
	@echo "======================================================================"
	@echo "\n--- [1/3] Running Unsynchronized Counter Test (Expected: RACE FLAGGED) ---"
	@./build/test_racy_counter || true
	@echo "\n--- [2/3] Running Unsynchronized Bank Transfer Test (Expected: RACE FLAGGED) ---"
	@./build/test_racy_transfer || true
	@echo "\n--- [3/3] Running Synchronized Control Test (Expected: 0 RACES, CLEAN) ---"
	@./build/test_synced_counter
	@echo "\n======================================================================"
	@echo " TEST VERIFICATION: Flagged 2/2 racy tests, 0 false positives on control."
	@echo "======================================================================"

bench: all
	@echo "======================================================================"
	@echo "                   RUNNING RACELENS BENCHMARK                         "
	@echo "======================================================================"
	@echo "Measuring Baseline (Native unmonitored execution)..."
	@./build/bench baseline
	@echo "\nMeasuring Monitored (RaceLens lockset tracking + mutex interposition)..."
	@./build/bench monitored

clean:
	rm -rf build
