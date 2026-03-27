#define _POSIX_C_SOURCE 199309L
#include "racelens.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define BENCH_ITERATIONS 500000

static int g_bench_counter = 0;
static pthread_mutex_t g_bench_mutex = PTHREAD_MUTEX_INITIALIZER;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void *bench_worker_monitored(void *arg) {
    (void)arg;
    for (int i = 0; i < BENCH_ITERATIONS; ++i) {
        pthread_mutex_lock(&g_bench_mutex);
        int v = RL_READ(g_bench_counter);
        RL_WRITE(g_bench_counter, v + 1);
        pthread_mutex_unlock(&g_bench_mutex);
    }
    return NULL;
}

void *bench_worker_unmonitored(void *arg) {
    (void)arg;
    for (int i = 0; i < BENCH_ITERATIONS; ++i) {
        pthread_mutex_lock(&g_bench_mutex);
        g_bench_counter++;
        pthread_mutex_unlock(&g_bench_mutex);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int run_monitored = (argc > 1 && argv[1][0] == 'm');

    printf("[RaceLens Benchmark] Running in %s mode (%d iterations x 2 threads)...\n",
           run_monitored ? "MONITORED (with RaceLens)" : "BASELINE (unmonitored)",
           BENCH_ITERATIONS);

    g_bench_counter = 0;
    double t_start = get_time_sec();

    pthread_t t1, t2;
    if (run_monitored) {
        pthread_create(&t1, NULL, bench_worker_monitored, NULL);
        pthread_create(&t2, NULL, bench_worker_monitored, NULL);
    } else {
        pthread_create(&t1, NULL, bench_worker_unmonitored, NULL);
        pthread_create(&t2, NULL, bench_worker_unmonitored, NULL);
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    double t_end = get_time_sec();
    double elapsed = t_end - t_start;

    printf("[RaceLens Benchmark] Result: Elapsed = %.4f seconds (Final Counter = %d)\n",
           elapsed, g_bench_counter);
    return 0;
}
