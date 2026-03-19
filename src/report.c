#include "report.h"
#include "shadow.h"
#include "interceptor.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_CYAN    "\033[1;36m"

const char *racelens_state_to_string(eraser_state_t state) {
    switch (state) {
        case STATE_VIRGIN:          return "VIRGIN";
        case STATE_EXCLUSIVE:       return "EXCLUSIVE";
        case STATE_SHARED:          return "SHARED";
        case STATE_SHARED_MODIFIED: return "SHARED-MODIFIED";
        default:                    return "UNKNOWN";
    }
}

void racelens_report_race(const shadow_entry_t *entry,
                          bool is_write,
                          const char *access_file,
                          int access_line,
                          uint32_t thread_id,
                          const lockset_t *held_locks) {
    bool use_color = isatty(STDERR_FILENO);

    fprintf(stderr, "\n%s======================================================================%s\n",
            use_color ? COLOR_RED : "", use_color ? COLOR_RESET : "");
    fprintf(stderr, "%s[RaceLens] WARNING: DATA RACE DETECTED!%s\n",
            use_color ? COLOR_RED : "", use_color ? COLOR_RESET : "");
    fprintf(stderr, "%s======================================================================%s\n",
            use_color ? COLOR_RED : "", use_color ? COLOR_RESET : "");

    fprintf(stderr, "  %sTarget Variable:%s %s (addr: 0x%lx)\n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "",
            entry->var_name[0] ? entry->var_name : "<anonymous>",
            (unsigned long)entry->addr);

    fprintf(stderr, "  %sAccess Location:%s %s:%d\n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "",
            access_file ? access_file : "<unknown>", access_line);

    fprintf(stderr, "  %sCurrent Access:%s  %s by Thread #%u\n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "",
            is_write ? "WRITE" : "READ", thread_id);

    fprintf(stderr, "  %sLocks Held:%s      ",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "");
    if (!held_locks || held_locks->count == 0) {
        fprintf(stderr, "%sNONE (Thread holds zero protecting mutexes)%s\n",
                use_color ? COLOR_YELLOW : "", use_color ? COLOR_RESET : "");
    } else {
        fprintf(stderr, "%zu lock(s) [", held_locks->count);
        for (size_t i = 0; i < held_locks->count; ++i) {
            fprintf(stderr, "0x%lx%s", (unsigned long)held_locks->locks[i],
                    (i + 1 < held_locks->count) ? ", " : "");
        }
        fprintf(stderr, "]\n");
    }

    fprintf(stderr, "  %sEraser State:%s    %s\n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "",
            racelens_state_to_string(entry->state));

    fprintf(stderr, "  %sCandidate Set:%s   EMPTY (C(v) == ∅)\n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "");

    fprintf(stderr, "  %sRoot Cause:%s       Variable transitioned to SHARED-MODIFIED, but no\n"
                    "                     single mutex protected all concurrent accesses.\n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "");

    fprintf(stderr, "%s======================================================================%s\n\n",
            use_color ? COLOR_RED : "", use_color ? COLOR_RESET : "");
}

void racelens_print_summary(void) {
    bool use_color = isatty(STDERR_FILENO);
    size_t races = shadow_table_get_total_races();
    size_t vars = shadow_table_get_total_vars();
    size_t accesses = shadow_table_get_total_accesses();
    size_t locks = racelens_get_mutex_lock_count();
    size_t unlocks = racelens_get_mutex_unlock_count();

    fprintf(stderr, "\n%s======================================================================%s\n",
            use_color ? COLOR_CYAN : "", use_color ? COLOR_RESET : "");
    fprintf(stderr, "                     %sRaceLens Execution Summary%s                      \n",
            use_color ? COLOR_BOLD : "", use_color ? COLOR_RESET : "");
    fprintf(stderr, "%s======================================================================%s\n",
            use_color ? COLOR_CYAN : "", use_color ? COLOR_RESET : "");
    fprintf(stderr, "  Tracked Variables:         %zu\n", vars);
    fprintf(stderr, "  Memory Accesses Monitored: %zu\n", accesses);
    fprintf(stderr, "  Mutex Lock / Unlock Ops:   %zu / %zu\n", locks, unlocks);
    fprintf(stderr, "  Data Races Flagged:        %s%zu%s\n",
            races > 0 ? (use_color ? COLOR_RED : "") : (use_color ? COLOR_GREEN : ""),
            races,
            use_color ? COLOR_RESET : "");
    fprintf(stderr, "  Final Verification:        %s\n",
            races > 0 ? (use_color ? COLOR_RED "FAILED (Races Detected)" COLOR_RESET : "FAILED (Races Detected)")
                      : (use_color ? COLOR_GREEN "PASSED (Clean Execution)" COLOR_RESET : "PASSED (Clean Execution)"));
    fprintf(stderr, "%s======================================================================%s\n\n",
            use_color ? COLOR_CYAN : "", use_color ? COLOR_RESET : "");
}
