# RaceLens – Pthread Data Race Detector

[![CI](https://github.com/hagemaruwu/racelens/actions/workflows/ci.yml/badge.svg)](https://github.com/hagemaruwu/racelens/actions)
[![Language](https://img.shields.io/badge/Language-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Standard](https://img.shields.io/badge/POSIX-pthreads-brightgreen.svg)](https://pubs.opengroup.org/onlinepubs/9699919799/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Overhead](https://img.shields.io/badge/Runtime%20Overhead-%7E19%25-success.svg)](benchmark/)

**RaceLens** is a dynamic concurrency race detector for multithreaded C programs. It combines **dynamic symbol interposition** (`LD_PRELOAD` via `dlsym` / `RTLD_NEXT`) with a lock-striped implementation of the classic **Eraser lockset algorithm** (Savage et al., 1997).

RaceLens monitors POSIX synchronization primitives at runtime without modifying the system `libc`, tracking per-thread held mutexes and intersecting candidate locksets across instrumented memory accesses to flag data races before they cause non-deterministic concurrency bugs in production.

---

## Table of Contents

- [The Problem: Concurrency & Data Races](#the-problem-concurrency--data-races)
- [How RaceLens Works](#how-racelens-works)
  - [1. Dynamic Interposition & Reentrancy Guards](#1-dynamic-interposition--reentrancy-guards)
  - [2. The Eraser Lockset Algorithm](#2-the-eraser-lockset-algorithm)
  - [3. Striped Shadow Memory Architecture](#3-striped-shadow-memory-architecture)
- [System Architecture](#system-architecture)
- [Project Layout](#project-layout)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Building from Source](#building-from-source)
  - [Running the Validation Test Suite](#running-the-validation-test-suite)
  - [Running Performance Benchmarks](#running-performance-benchmarks)
- [Validation Test Suite Walkthrough](#validation-test-suite-walkthrough)
  - [Test 1: Unsynchronized Counter](#test-1-unsynchronized-counter)
  - [Test 2: Unsynchronized Account Transfer](#test-2-unsynchronized-account-transfer)
  - [Test 3: Synchronized Control (Zero False Positives)](#test-3-synchronized-control-zero-false-positives)
- [Instrumenting Your Own Applications](#instrumenting-your-own-applications)
- [Performance & Overhead Analysis](#performance--overhead-analysis)
- [License](#license)

---

## The Problem: Concurrency & Data Races

Under the POSIX and ISO C11 memory models, a **data race** occurs whenever two or more concurrent threads access the same memory location simultaneously without synchronization, and **at least one of the accesses is a write**.

Data races are among the most insidious bugs in systems software:
1. **Non-deterministic Heisenbugs:** Manifest intermittently depending on thread preemption, CPU cache pressure, and OS scheduling.
2. **Hardware Reordering:** Modern out-of-order CPUs (x86 store buffers, ARM weak memory models) reorder memory stores, causing silent data corruption that cannot be reproduced in debuggers.
3. **Compiler Transformations:** Compilers assume data-race freedom and optimize code accordingly (e.g., hoisting reads out of loops or inventing stores), turning unsynchronized accesses into undefined behavior.

Pure static analysis tools suffer from high false-positive rates due to pointer aliasing. Heavy dynamic binary translation tools (like Valgrind Helgrind/DRD) impose a **20x–50x runtime slowdown**. RaceLens implements a focused runtime engine delivering precise race detection at **~19% runtime overhead**.

---

## How RaceLens Works

### 1. Dynamic Interposition & Reentrancy Guards

RaceLens intercepts POSIX thread synchronization calls (`pthread_mutex_lock`, `pthread_mutex_unlock`, `pthread_mutex_trylock`) using dynamic linking interposition:
- On **Linux**, the dynamic loader (`ld.so`) loads `libracelens.so` via `LD_PRELOAD`.
- Function pointers to real implementations in `libc` are resolved on-demand using `dlsym(RTLD_NEXT, "symbol_name")`.

#### The Reentrancy Trap
When intercepting synchronization primitives, internal `libc` operations (such as `malloc()`, `printf()`, or dynamic linker symbol lookups) often acquire internal mutexes. If an interceptor attempts to log or allocate memory while processing an interception, it will trigger an **infinite recursion loop**, leading to a stack overflow before `main()` executes.

RaceLens solves this using a zero-allocation, thread-local reentrancy guard:
```c
static __thread int g_in_detector = 0;

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (g_in_detector || !real_pthread_mutex_lock) {
        return real_pthread_mutex_lock ? real_pthread_mutex_lock(mutex) : 0;
    }
    
    g_in_detector++;
    int res = real_pthread_mutex_lock(mutex);
    if (res == 0) {
        racelens_on_mutex_lock((uintptr_t)mutex);
    }
    g_in_detector--;
    return res;
}
```

---

### 2. The Eraser Lockset Algorithm

RaceLens is built upon the classic **Eraser** lockset algorithm (Savage et al., ACM TOCS 1997).

#### The Core Invariant
> *Every shared variable must be consistently protected by at least one mutex throughout its entire lifetime across all concurrent accesses.*

For each monitored memory location $v$:
1. $C(v)$ represents the **candidate lockset** protecting $v$.
2. $H(t)$ represents the set of **locks currently held** by thread $t$.
3. On each access to $v$ by thread $t$, the candidate set is updated via set intersection:
   $$\large C(v) \leftarrow C(v) \cap H(t)$$
4. If $C(v)$ becomes **empty** ($\emptyset$) while the variable is in an actively shared, modified state, an invariant violation has occurred: **a data race is flagged immediately.**

#### Finite State Machine Transitions

```
                   Write by Thread 1
  [ VIRGIN ] -----------------------------> [ EXCLUSIVE (Thread 1) ]
                                                   |
                               Read by Thread 2    | Write by Thread 2
                               +-------------------+------------------+
                               |                                      |
                               v                                      v
                          [ SHARED ] ------------------------> [ SHARED-MODIFIED ]
                               |             Write                    |
                               +--------------------------------------+
                                                  |
                                    C(v) == ∅ (Lockset Empty)
                                                  v
                                         [! DATA RACE FLAGGED !]
```

- **Virgin:** Newly allocated memory. Never accessed by any thread.
- **Exclusive:** Accessed exclusively by a single thread so far. Mutex locks are **not required** during this phase, correctly supporting unsynchronized thread-local initialization patterns without false positives.
- **Shared:** Read by multiple threads, but never written. Read-only sharing is thread-safe without locks.
- **Shared-Modified:** Accessed by multiple threads and at least one write has occurred. The lockset invariant is strictly enforced: $C(v) \leftarrow C(v) \cap H(t)$. If $C(v) = \emptyset$, a race is flagged.

---

### 3. Striped Shadow Memory Architecture

Monitoring every memory access in a multithreaded application can quickly bottleneck if protected by a single global lock. 

RaceLens utilizes a **64-way bucket-striped shadow memory table**:
- **Address Hashing:** Pointers are mapped into 1024 hash buckets using a 64-bit integer mix hash:
  ```c
  static inline size_t hash_address(uintptr_t addr) {
      addr ^= addr >> 33;
      addr *= 0xff51afd7ed558ccdULL;
      addr ^= addr >> 33;
      addr *= 0xc4ceb9fe1a85ec53ULL;
      addr ^= addr >> 33;
      return (size_t)(addr % SHADOW_BUCKET_COUNT);
  }
  ```
- **Striped Mutexes:** Buckets are partitioned across 64 dedicated mutexes:
  $$\text{lock\_index} = \left(\frac{\text{addr}}{16}\right) \pmod{64}$$
  This ensures that threads accessing different memory addresses execute in parallel on separate CPU cores without lock contention.

---

## System Architecture

```
                      +---------------------------------------+
                      |       Target Multithreaded App        |
                      +-------------------+-------------------+
                                          |
                   Memory Read / Write    |   pthread_mutex_* Calls
                     (RL_READ / WRITE)    |
                                          v
                      +---------------------------------------+
                      |     Dynamic Interceptor (LD_PRELOAD)  |
                      |   - Reentrancy Guard (thread_local)   |
                      |   - Real dlsym(RTLD_NEXT) Forwarding  |
                      +-------------------+-------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |         Eraser Lockset Engine         |
                      |   - Thread Held Locks: H(t)           |
                      |   - Candidate Locks:   C(v)           |
                      |   - Transition: C(v) <- C(v) ∩ H(t)   |
                      +-------------------+-------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |         Shadow Memory Table           |
                      |   - 1024 Buckets, 64 Striped Mutexes  |
                      |   - State: Virgin/Excl/Shared/Mod     |
                      +-------------------+-------------------+
                                          |
                             If C(v) == ∅ in Shared-Mod
                                          v
                      +---------------------------------------+
                      |        RaceLens Report Handler        |
                      |   - Exact File, Line, Thread, Addr    |
                      +-------------------+-------------------+
```

---

## Project Layout

```
RaceLens/
├── include/
│   ├── racelens.h            # Public macros (RL_READ, RL_WRITE) and API
│   ├── engine.h              # Eraser state transitions and lockset math
│   ├── shadow.h              # 64-way bucket-striped shadow memory table
│   ├── interceptor.h         # Dynamic symbol hooks and recursion guards
│   └── report.h              # Diagnostic warning and summary formatters
├── src/
│   ├── engine.c              # Eraser lockset intersection and state logic
│   ├── shadow.c              # Hash table & striped synchronization
│   ├── interceptor.c         # dlsym(RTLD_NEXT) pthread_mutex_* interposition
│   └── report.c              # Diagnostic warnings & destructor summary
├── tests/
│   ├── test_racy_counter.c   # Unsynchronized counter test (2 threads)
│   ├── test_racy_transfer.c  # Unsynchronized bank balance transfer test
│   └── test_synced_counter.c # Synchronized control test (0 false positives)
├── benchmark/
│   ├── bench.c               # Performance benchmark runner
│   └── run_bench.py          # Python multi-run benchmark driver
├── bin/
│   └── racelens              # CLI launcher (LD_PRELOAD on Linux, DYLD on macOS)
├── .github/
│   └── workflows/ci.yml      # Automated GitHub Actions Linux CI
├── Makefile                  # Portable build configuration
├── LICENSE                   # MIT License
└── README.md                 # Project documentation
```

---

## Getting Started

### Prerequisites
- GCC or Clang (C99 support)
- POSIX Threads (`libpthread`)
- Linux or macOS
- Make and Python 3 (for benchmark runner)

### Building from Source

Clone the repository and compile:
```bash
git clone https://github.com/hagemaruwu/racelens.git
cd racelens
make
```

This compiles:
1. `build/libracelens.so` (or `.dylib` on macOS) — The core detector library.
2. `build/test_*` — The test binaries.
3. `build/bench` — The benchmark binary.

---

### Running the Validation Test Suite

Execute the automated test suite:
```bash
make test
```

This runs:
1. `test_racy_counter`: Unsynchronized shared counter with 2 threads $\to$ **RACE FLAGGED**.
2. `test_racy_transfer`: Unsynchronized bank transfer with concurrent accounts $\to$ **RACE FLAGGED**.
3. `test_synced_counter`: Mutex-protected counter control $\to$ **PASSED (0 Races, Clean)**.

---

### Running Performance Benchmarks

To measure the runtime overhead of RaceLens compared to unmonitored execution:
```bash
python3 benchmark/run_bench.py
```

Sample benchmark output:
```
=================================================================
           RaceLens Runtime Overhead Benchmark (2026)
=================================================================
Measuring unmonitored baseline (5 iterations)...
  -> Baseline average time:  0.0261 s
Measuring RaceLens monitored execution (5 iterations)...
  -> Monitored average time: 0.0310 s
-----------------------------------------------------------------
  Baseline Execution:  26.10 ms
  Monitored Execution: 31.05 ms
  Runtime Overhead:    +18.9%
=================================================================
```

---

## Validation Test Suite Walkthrough

### Test 1: Unsynchronized Counter
**File:** [`tests/test_racy_counter.c`](file:///Users/adityaraj/Desktop/proj/OS/tests/test_racy_counter.c)

Two worker threads concurrently increment a shared variable `g_counter` 50,000 times each without a mutex:
```c
void *worker_thread(void *arg) {
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        int val = RL_READ(g_counter);
        RL_WRITE(g_counter, val + 1);
    }
    return NULL;
}
```

**Diagnostic Warning Flagged by RaceLens:**
```
======================================================================
[RaceLens] WARNING: DATA RACE DETECTED!
======================================================================
  Target Variable: g_counter (addr: 0x102c50000)
  Access Location: tests/test_racy_counter.c:13
  Current Access:  WRITE by Thread #1
  Locks Held:      NONE (Thread holds zero protecting mutexes)
  Eraser State:    SHARED-MODIFIED
  Candidate Set:   EMPTY (C(v) == ∅)
  Root Cause:       Variable transitioned to SHARED-MODIFIED, but no
                     single mutex protected all concurrent accesses.
======================================================================
Final Verification: FAILED (Races Detected)
```

---

### Test 2: Unsynchronized Account Transfer
**File:** [`tests/test_racy_transfer.c`](file:///Users/adityaraj/Desktop/proj/OS/tests/test_racy_transfer.c)

Thread 1 transfers funds from `Account A` to `Account B`, while Thread 2 concurrently transfers from `Account B` to `Account A`, without synchronization.

**Diagnostic Warnings Flagged by RaceLens:**
- Flags unsynchronized concurrent writes on `g_account_b`.
- Flags unsynchronized concurrent writes on `g_account_a`.
- Flags total balance corruption.

---

### Test 3: Synchronized Control (Zero False Positives)
**File:** [`tests/test_synced_counter.c`](file:///Users/adityaraj/Desktop/proj/OS/tests/test_synced_counter.c)

Two worker threads concurrently increment `g_counter`, but each read and write is strictly guarded by `pthread_mutex_lock(&g_counter_mutex)`:
```c
void *worker_thread(void *arg) {
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        pthread_mutex_lock(&g_counter_mutex);
        int val = RL_READ(g_counter);
        RL_WRITE(g_counter, val + 1);
        pthread_mutex_unlock(&g_counter_mutex);
    }
    return NULL;
}
```

**Clean Execution Summary Generated by RaceLens:**
```
======================================================================
                     RaceLens Execution Summary                      
======================================================================
  Tracked Variables:         1
  Memory Accesses Monitored: 199,999
  Mutex Lock / Unlock Ops:   100,000 / 100,000
  Data Races Flagged:        0
  Final Verification:        PASSED (Clean Execution)
======================================================================
```

---

## Instrumenting Your Own Applications

To use RaceLens in your own C applications:

1. Include the public header:
   ```c
   #include "racelens.h"
   ```

2. Wrap your shared memory reads and writes:
   ```c
   int current_val = RL_READ(shared_variable);
   RL_WRITE(shared_variable, current_val + 1);
   ```

3. Launch your program through the RaceLens runner:
   ```bash
   ./bin/racelens ./my_multithreaded_program
   ```

On exit, RaceLens automatically outputs a complete execution summary report.

---

## Performance & Overhead Analysis

| Component | Design Choice | Performance Impact |
| :--- | :--- | :--- |
| **Shadow Table Mutexes** | 64-way bucket-striped mutexes | Eliminates core contention on multi-core CPUs |
| **Hash Function** | 64-bit integer mix hash (`Murmur`-style) | Sub-nanosecond bucket indexing |
| **Lockset Math** | Fixed-capacity flat arrays with linear scan | Keeps candidate sets in L1 CPU cache |
| **Dynamic Linking** | `dlsym(RTLD_NEXT)` cached function pointers | Zero per-call symbol lookup overhead |
| **Overall Overhead** | Target $\le 20\%$ | **Measured: ~18.9%** |

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
