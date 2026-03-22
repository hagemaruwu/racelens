#include "racelens.h"
#include <stdio.h>
#include <pthread.h>

#define NUM_ITERATIONS 50000

static int g_counter = 0;

void *worker_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        int val = RL_READ(g_counter);
        RL_WRITE(g_counter, val + 1);
    }
    return NULL;
}

int main(void) {
    printf("[Test 1] Starting Unsynchronized Counter Test (2 threads, %d iterations)...\n", NUM_ITERATIONS);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker_thread, NULL);
    pthread_create(&t2, NULL, worker_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[Test 1] Completed. Final Counter Value = %d (Expected: %d without race)\n",
           g_counter, NUM_ITERATIONS * 2);
    return 0;
}
