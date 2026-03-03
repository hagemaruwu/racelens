#ifndef RACELENS_H
#define RACELENS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Record a memory read access for an instrumented variable.
 */
void racelens_read_access(const void *addr, const char *file, int line, const char *var_name);

/**
 * Record a memory write access for an instrumented variable.
 */
void racelens_write_access(const void *addr, const char *file, int line, const char *var_name);

/**
 * Print the final race detection summary.
 * (Also registered as a library destructor on exit).
 */
void racelens_dump_summary(void);

/**
 * Macro wrappers for convenient access instrumentation.
 */
#define RL_READ(var) \
    (racelens_read_access(&(var), __FILE__, __LINE__, #var), (var))

#define RL_WRITE(var, val) \
    do { \
        racelens_write_access(&(var), __FILE__, __LINE__, #var); \
        (var) = (val); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* RACELENS_H */
