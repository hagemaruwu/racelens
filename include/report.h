#ifndef RACELENS_REPORT_H
#define RACELENS_REPORT_H

#include "engine.h"
#include "shadow.h"
#include <stdbool.h>

void racelens_report_race(const shadow_entry_t *entry,
                          bool is_write,
                          const char *access_file,
                          int access_line,
                          uint32_t thread_id,
                          const lockset_t *held_locks);

void racelens_print_summary(void);

const char *racelens_state_to_string(eraser_state_t state);

#endif /* RACELENS_REPORT_H */
