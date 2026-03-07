#ifndef RACELENS_ENGINE_H
#define RACELENS_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

#define RACELENS_MAX_LOCKS 32

/**
 * Savage et al. (1997) Eraser memory location states.
 */
typedef enum {
    STATE_VIRGIN = 0,         /* Allocated, never accessed by any thread */
    STATE_EXCLUSIVE = 1,      /* Accessed by exactly one thread so far (initialization safe) */
    STATE_SHARED = 2,         /* Read by multiple threads, never written concurrently */
    STATE_SHARED_MODIFIED = 3 /* Accessed by multiple threads and at least one write */
} eraser_state_t;

/**
 * Representation of a set of locks (mutex pointer addresses).
 */
typedef struct {
    uintptr_t locks[RACELENS_MAX_LOCKS];
    size_t count;
} lockset_t;

/* Lockset manipulation operations */
void lockset_init(lockset_t *ls);
void lockset_copy(lockset_t *dest, const lockset_t *src);
bool lockset_add(lockset_t *ls, uintptr_t lock);
bool lockset_remove(lockset_t *ls, uintptr_t lock);
bool lockset_contains(const lockset_t *ls, uintptr_t lock);
void lockset_intersect(lockset_t *dest, const lockset_t *src);
bool lockset_is_empty(const lockset_t *ls);

/* Mutex interception hooks */
void racelens_on_mutex_lock(uintptr_t mutex);
void racelens_on_mutex_unlock(uintptr_t mutex);

/* Thread context helpers */
lockset_t *racelens_get_current_held_locks(void);
uint32_t racelens_get_current_thread_id(void);

/* Core Eraser transitions */
void racelens_engine_access(const void *addr, bool is_write,
                            const char *file, int line, const char *var_name);

#endif /* RACELENS_ENGINE_H */
