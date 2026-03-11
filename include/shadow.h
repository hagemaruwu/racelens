#ifndef RACELENS_SHADOW_H
#define RACELENS_SHADOW_H

#include "engine.h"
#include <stdbool.h>
#include <pthread.h>

#define SHADOW_BUCKET_COUNT 1024
#define SHADOW_LOCK_COUNT   64

typedef struct shadow_entry {
    uintptr_t addr;
    eraser_state_t state;
    pthread_t first_thread;
    uint32_t first_thread_id;
    lockset_t candidate_locks;
    size_t reads;
    size_t writes;
    bool race_reported;
    char var_name[64];
    char first_file[128];
    int first_line;
    struct shadow_entry *next;
} shadow_entry_t;

typedef struct {
    shadow_entry_t *buckets[SHADOW_BUCKET_COUNT];
    pthread_mutex_t locks[SHADOW_LOCK_COUNT];
    size_t total_tracked_vars;
    size_t total_accesses;
    size_t total_races_flagged;
} shadow_table_t;

void shadow_table_init(void);
shadow_entry_t *shadow_table_get_or_create(const void *addr, const char *var_name,
                                           const char *file, int line,
                                           pthread_mutex_t **out_lock);
void shadow_table_lock_bucket(uintptr_t addr);
void shadow_table_unlock_bucket(uintptr_t addr);

/* Diagnostic counters */
size_t shadow_table_get_total_vars(void);
size_t shadow_table_get_total_accesses(void);
size_t shadow_table_get_total_races(void);
void shadow_table_inc_access(void);
void shadow_table_inc_races(void);

#endif /* RACELENS_SHADOW_H */
