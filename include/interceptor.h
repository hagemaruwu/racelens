#ifndef RACELENS_INTERCEPTOR_H
#define RACELENS_INTERCEPTOR_H

#include <stdbool.h>
#include <pthread.h>
#include <stddef.h>

void racelens_interceptor_init(void);

/* Recursion / reentrancy guard for interceptors */
bool racelens_is_in_detector(void);
void racelens_enter_detector(void);
void racelens_exit_detector(void);

/* Diagnostic counter for mutex ops */
size_t racelens_get_mutex_lock_count(void);
size_t racelens_get_mutex_unlock_count(void);

#endif /* RACELENS_INTERCEPTOR_H */
