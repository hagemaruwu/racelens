#include "racelens.h"
#include <stdio.h>
#include <pthread.h>

#define NUM_ITERATIONS 50000

static int g_counter = 0;
static pthread_mutex_t g_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

void *worker_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        pthread_mutex_lock(&g_counter_mutex);
        int val = RL_READ(g_counter);
        RL_WRITE(g_counter, val + 1);
        pthread_mutex_unlock(&g_counter_mutex);
    }
    return NULL;
}

int main(void) {
    printf("[Control Test] Starting Synchronized Counter Test (2 threads, %d iterations)...\n", NUM_ITERATIONS);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker_thread, NULL);
    pthread_create(&t2, NULL, worker_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[Control Test] Completed. Final Counter Value = %d (Expected: %d)\n",
           g_counter, NUM_ITERATIONS * 2);
    return 0;
}
