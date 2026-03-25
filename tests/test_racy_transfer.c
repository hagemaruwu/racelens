#include "racelens.h"
#include <stdio.h>
#include <pthread.h>

#define NUM_TRANSFERS 30000

static int g_account_a = 100000;
static int g_account_b = 100000;

void *transfer_a_to_b(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_TRANSFERS; ++i) {
        int a = RL_READ(g_account_a);
        RL_WRITE(g_account_a, a - 10);

        int b = RL_READ(g_account_b);
        RL_WRITE(g_account_b, b + 10);
    }
    return NULL;
}

void *transfer_b_to_a(void *arg) {
    (void)arg;
    for (int i = 0; i < NUM_TRANSFERS; ++i) {
        int b = RL_READ(g_account_b);
        RL_WRITE(g_account_b, b - 10);

        int a = RL_READ(g_account_a);
        RL_WRITE(g_account_a, a + 10);
    }
    return NULL;
}

int main(void) {
    printf("[Test 2] Starting Unsynchronized Account Transfer Test (%d transfers)...\n", NUM_TRANSFERS);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, transfer_a_to_b, NULL);
    pthread_create(&t2, NULL, transfer_b_to_a, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("[Test 2] Completed. Account A = %d, Account B = %d (Total: %d)\n",
           g_account_a, g_account_b, g_account_a + g_account_b);
    return 0;
}
