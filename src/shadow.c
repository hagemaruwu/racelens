#include "shadow.h"
#include <stdlib.h>
#include <string.h>

static shadow_table_t g_shadow_table;
static pthread_once_t g_shadow_once = PTHREAD_ONCE_INIT;

static inline size_t hash_address(uintptr_t addr) {
    /* Murmur-inspired fast integer hash */
    addr ^= addr >> 33;
    addr *= 0xff51afd7ed558ccdULL;
    addr ^= addr >> 33;
    addr *= 0xc4ceb9fe1a85ec53ULL;
    addr ^= addr >> 33;
    return (size_t)(addr % SHADOW_BUCKET_COUNT);
}

static inline size_t lock_index_for_addr(uintptr_t addr) {
    return (addr >> 4) % SHADOW_LOCK_COUNT;
}

static void shadow_table_init_internal(void) {
    memset(g_shadow_table.buckets, 0, sizeof(g_shadow_table.buckets));
    for (size_t i = 0; i < SHADOW_LOCK_COUNT; ++i) {
        pthread_mutex_init(&g_shadow_table.locks[i], NULL);
    }
    g_shadow_table.total_tracked_vars = 0;
    g_shadow_table.total_accesses = 0;
    g_shadow_table.total_races_flagged = 0;
}

void shadow_table_init(void) {
    pthread_once(&g_shadow_once, shadow_table_init_internal);
}

void shadow_table_lock_bucket(uintptr_t addr) {
    shadow_table_init();
    size_t lidx = lock_index_for_addr(addr);
    pthread_mutex_lock(&g_shadow_table.locks[lidx]);
}

void shadow_table_unlock_bucket(uintptr_t addr) {
    size_t lidx = lock_index_for_addr(addr);
    pthread_mutex_unlock(&g_shadow_table.locks[lidx]);
}

shadow_entry_t *shadow_table_get_or_create(const void *addr, const char *var_name,
                                           const char *file, int line,
                                           pthread_mutex_t **out_lock) {
    shadow_table_init();
    uintptr_t ptr = (uintptr_t)addr;
    size_t bidx = hash_address(ptr);
    size_t lidx = lock_index_for_addr(ptr);

    if (out_lock) {
        *out_lock = &g_shadow_table.locks[lidx];
    }

    shadow_entry_t *curr = g_shadow_table.buckets[bidx];
    while (curr) {
        if (curr->addr == ptr) {
            return curr;
        }
        curr = curr->next;
    }

    /* Not found: allocate a new shadow entry */
    shadow_entry_t *entry = (shadow_entry_t *)malloc(sizeof(shadow_entry_t));
    if (!entry) return NULL;

    memset(entry, 0, sizeof(shadow_entry_t));
    entry->addr = ptr;
    entry->state = STATE_VIRGIN;
    entry->first_thread = 0;
    entry->first_thread_id = 0;
    lockset_init(&entry->candidate_locks);
    entry->reads = 0;
    entry->writes = 0;
    entry->race_reported = false;

    if (var_name) {
        strncpy(entry->var_name, var_name, sizeof(entry->var_name) - 1);
        entry->var_name[sizeof(entry->var_name) - 1] = '\0';
    }
    if (file) {
        strncpy(entry->first_file, file, sizeof(entry->first_file) - 1);
        entry->first_file[sizeof(entry->first_file) - 1] = '\0';
    }
    entry->first_line = line;

    entry->next = g_shadow_table.buckets[bidx];
    g_shadow_table.buckets[bidx] = entry;
    g_shadow_table.total_tracked_vars++;

    return entry;
}

size_t shadow_table_get_total_vars(void) {
    return g_shadow_table.total_tracked_vars;
}

size_t shadow_table_get_total_accesses(void) {
    return g_shadow_table.total_accesses;
}

size_t shadow_table_get_total_races(void) {
    return g_shadow_table.total_races_flagged;
}

void shadow_table_inc_access(void) {
    __sync_fetch_and_add(&g_shadow_table.total_accesses, 1);
}

void shadow_table_inc_races(void) {
    __sync_fetch_and_add(&g_shadow_table.total_races_flagged, 1);
}
