#include "engine.h"
#include "shadow.h"
#include "report.h"
#include "interceptor.h"
#include <string.h>
#include <stdio.h>

static __thread lockset_t t_held_locks = {.count = 0};
static __thread uint32_t  t_thread_id  = 0;
static uint32_t g_next_thread_id = 1;

void lockset_init(lockset_t *ls) {
    if (!ls) return;
    ls->count = 0;
    memset(ls->locks, 0, sizeof(ls->locks));
}

void lockset_copy(lockset_t *dest, const lockset_t *src) {
    if (!dest || !src) return;
    dest->count = src->count;
    memcpy(dest->locks, src->locks, src->count * sizeof(uintptr_t));
}

bool lockset_contains(const lockset_t *ls, uintptr_t lock) {
    if (!ls) return false;
    for (size_t i = 0; i < ls->count; ++i) {
        if (ls->locks[i] == lock) return true;
    }
    return false;
}

bool lockset_add(lockset_t *ls, uintptr_t lock) {
    if (!ls || lockset_contains(ls, lock)) return false;
    if (ls->count >= RACELENS_MAX_LOCKS) return false;
    ls->locks[ls->count++] = lock;
    return true;
}

bool lockset_remove(lockset_t *ls, uintptr_t lock) {
    if (!ls) return false;
    for (size_t i = 0; i < ls->count; ++i) {
        if (ls->locks[i] == lock) {
            ls->locks[i] = ls->locks[ls->count - 1];
            ls->count--;
            return true;
        }
    }
    return false;
}

void lockset_intersect(lockset_t *dest, const lockset_t *src) {
    if (!dest) return;
    if (!src || src->count == 0) {
        dest->count = 0;
        return;
    }
    size_t new_count = 0;
    for (size_t i = 0; i < dest->count; ++i) {
        if (lockset_contains(src, dest->locks[i])) {
            dest->locks[new_count++] = dest->locks[i];
        }
    }
    dest->count = new_count;
}

bool lockset_is_empty(const lockset_t *ls) {
    return (!ls || ls->count == 0);
}

uint32_t racelens_get_current_thread_id(void) {
    if (t_thread_id == 0) {
        t_thread_id = __sync_fetch_and_add(&g_next_thread_id, 1);
    }
    return t_thread_id;
}

lockset_t *racelens_get_current_held_locks(void) {
    return &t_held_locks;
}

void racelens_on_mutex_lock(uintptr_t mutex) {
    lockset_add(&t_held_locks, mutex);
}

void racelens_on_mutex_unlock(uintptr_t mutex) {
    lockset_remove(&t_held_locks, mutex);
}

void racelens_engine_access(const void *addr, bool is_write,
                            const char *file, int line, const char *var_name) {
    if (racelens_is_in_detector()) return;
    racelens_enter_detector();

    shadow_table_inc_access();
    uintptr_t ptr = (uintptr_t)addr;
    shadow_table_lock_bucket(ptr);

    shadow_entry_t *entry = shadow_table_get_or_create(addr, var_name, file, line, NULL);
    if (!entry) {
        shadow_table_unlock_bucket(ptr);
        racelens_exit_detector();
        return;
    }

    pthread_t curr_thread = pthread_self();
    uint32_t curr_tid = racelens_get_current_thread_id();

    if (is_write) {
        entry->writes++;
    } else {
        entry->reads++;
    }

    switch (entry->state) {
        case STATE_VIRGIN:
            entry->first_thread = curr_thread;
            entry->first_thread_id = curr_tid;
            entry->state = STATE_EXCLUSIVE;
            break;

        case STATE_EXCLUSIVE:
            if (pthread_equal(entry->first_thread, curr_thread)) {
                /* Same thread accessing: stays in EXCLUSIVE (thread-local initialization) */
            } else {
                /* Second thread accessed: transition out of EXCLUSIVE */
                lockset_copy(&entry->candidate_locks, &t_held_locks);
                if (!is_write) {
                    entry->state = STATE_SHARED;
                } else {
                    entry->state = STATE_SHARED_MODIFIED;
                    if (lockset_is_empty(&entry->candidate_locks) && !entry->race_reported) {
                        entry->race_reported = true;
                        shadow_table_inc_races();
                        racelens_report_race(entry, is_write, file, line, curr_tid, &t_held_locks);
                    }
                }
            }
            break;

        case STATE_SHARED:
            if (!is_write) {
                /* Concurrent reads: intersect candidate locks */
                lockset_intersect(&entry->candidate_locks, &t_held_locks);
            } else {
                /* Transition to SHARED-MODIFIED upon first write */
                entry->state = STATE_SHARED_MODIFIED;
                lockset_intersect(&entry->candidate_locks, &t_held_locks);
                if (lockset_is_empty(&entry->candidate_locks) && !entry->race_reported) {
                    entry->race_reported = true;
                    shadow_table_inc_races();
                    racelens_report_race(entry, is_write, file, line, curr_tid, &t_held_locks);
                }
            }
            break;

        case STATE_SHARED_MODIFIED:
            lockset_intersect(&entry->candidate_locks, &t_held_locks);
            if (lockset_is_empty(&entry->candidate_locks) && !entry->race_reported) {
                entry->race_reported = true;
                shadow_table_inc_races();
                racelens_report_race(entry, is_write, file, line, curr_tid, &t_held_locks);
            }
            break;
    }

    shadow_table_unlock_bucket(ptr);
    racelens_exit_detector();
}

/* Public instrumentation callbacks defined in racelens.h */
void racelens_read_access(const void *addr, const char *file, int line, const char *var_name) {
    racelens_engine_access(addr, false, file, line, var_name);
}

void racelens_write_access(const void *addr, const char *file, int line, const char *var_name) {
    racelens_engine_access(addr, true, file, line, var_name);
}

void racelens_dump_summary(void) {
    racelens_print_summary();
}
