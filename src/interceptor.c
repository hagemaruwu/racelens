#define _GNU_SOURCE
#include "interceptor.h"
#include "engine.h"
#include "shadow.h"
#include "report.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static __thread int g_in_detector = 0;
static size_t g_mutex_lock_count = 0;
static size_t g_mutex_unlock_count = 0;

static int (*real_pthread_mutex_lock)(pthread_mutex_t *) = NULL;
static int (*real_pthread_mutex_unlock)(pthread_mutex_t *) = NULL;
static int (*real_pthread_mutex_trylock)(pthread_mutex_t *) = NULL;

bool racelens_is_in_detector(void) {
    return g_in_detector > 0;
}

void racelens_enter_detector(void) {
    g_in_detector++;
}

void racelens_exit_detector(void) {
    if (g_in_detector > 0) {
        g_in_detector--;
    }
}

size_t racelens_get_mutex_lock_count(void) {
    return g_mutex_lock_count;
}

size_t racelens_get_mutex_unlock_count(void) {
    return g_mutex_unlock_count;
}

void racelens_interceptor_init(void) {
    if (!real_pthread_mutex_lock) {
        real_pthread_mutex_lock = (int (*)(pthread_mutex_t *))dlsym(RTLD_NEXT, "pthread_mutex_lock");
    }
    if (!real_pthread_mutex_unlock) {
        real_pthread_mutex_unlock = (int (*)(pthread_mutex_t *))dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    }
    if (!real_pthread_mutex_trylock) {
        real_pthread_mutex_trylock = (int (*)(pthread_mutex_t *))dlsym(RTLD_NEXT, "pthread_mutex_trylock");
    }
}

/* Intercepted pthread_mutex_lock */
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!real_pthread_mutex_lock) {
        racelens_interceptor_init();
    }

    if (racelens_is_in_detector() || !real_pthread_mutex_lock) {
        return real_pthread_mutex_lock ? real_pthread_mutex_lock(mutex) : 0;
    }

    racelens_enter_detector();
    int res = real_pthread_mutex_lock(mutex);
    if (res == 0) {
        __sync_fetch_and_add(&g_mutex_lock_count, 1);
        racelens_on_mutex_lock((uintptr_t)mutex);
    }
    racelens_exit_detector();
    return res;
}

/* Intercepted pthread_mutex_unlock */
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!real_pthread_mutex_unlock) {
        racelens_interceptor_init();
    }

    if (racelens_is_in_detector() || !real_pthread_mutex_unlock) {
        return real_pthread_mutex_unlock ? real_pthread_mutex_unlock(mutex) : 0;
    }

    racelens_enter_detector();
    racelens_on_mutex_unlock((uintptr_t)mutex);
    __sync_fetch_and_add(&g_mutex_unlock_count, 1);
    int res = real_pthread_mutex_unlock(mutex);
    racelens_exit_detector();
    return res;
}

/* Intercepted pthread_mutex_trylock */
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!real_pthread_mutex_trylock) {
        racelens_interceptor_init();
    }

    if (racelens_is_in_detector() || !real_pthread_mutex_trylock) {
        return real_pthread_mutex_trylock ? real_pthread_mutex_trylock(mutex) : 0;
    }

    racelens_enter_detector();
    int res = real_pthread_mutex_trylock(mutex);
    if (res == 0) {
        __sync_fetch_and_add(&g_mutex_lock_count, 1);
        racelens_on_mutex_lock((uintptr_t)mutex);
    }
    racelens_exit_detector();
    return res;
}

#if defined(__APPLE__)
/* Apple dynamic interposition tuples for DYLD_INSERT_LIBRARIES */
#define DYLD_INTERPOSE(_replacement,_replacee) \
   __attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
            __attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };

DYLD_INTERPOSE(pthread_mutex_lock, pthread_mutex_lock)
DYLD_INTERPOSE(pthread_mutex_unlock, pthread_mutex_unlock)
DYLD_INTERPOSE(pthread_mutex_trylock, pthread_mutex_trylock)
#endif

/* Destructor: automatically dump summary report when the process exits */
__attribute__((destructor))
static void racelens_cleanup(void) {
    racelens_print_summary();
}
