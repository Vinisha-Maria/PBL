#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "common.h"

/* Helper structure to track buffer position, remaining capacity, and error states */
typedef struct {
    char *buf;
    size_t size;
    size_t offset;
    int error;
} json_writer_t;

/* Initialize the json writer */
static void json_init(json_writer_t *w, char *buf, size_t size) {
    w->buf = buf;
    w->size = size;
    w->offset = 0;
    w->error = 0;
    if (size > 0) {
        w->buf[0] = '\0';
    }
}

/* Append formatted text safely to the json buffer */
static void json_append(json_writer_t *w, const char *format, ...) {
    if (w->error) return;
    if (w->offset >= w->size) {
        w->error = 1;
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(w->buf + w->offset, w->size - w->offset, format, args);
    va_end(args);

    // Check for encoding failure or buffer overflow
    if (written < 0 || (size_t)written >= w->size - w->offset) {
        w->error = 1;
    } else {
        w->offset += written;
    }
}

/* Append a string with proper JSON escaping for special characters and quotes */
static void json_append_escaped_string(json_writer_t *w, const char *str) {
    json_append(w, "\"");
    for (const char *p = str; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\"') {
            json_append(w, "\\\"");
        } else if (c == '\\') {
            json_append(w, "\\\\");
        } else if (c == '\n') {
            json_append(w, "\\n");
        } else if (c == '\r') {
            json_append(w, "\\r");
        } else if (c == '\t') {
            json_append(w, "\\t");
        } else if (c < 32) {
            json_append(w, "\\u00%02x", c);
        } else {
            char tmp[2] = { (char)c, '\0' };
            json_append(w, "%s", tmp);
        }
    }
    json_append(w, "\"");
}

/* Build the success JSON response string containing simulation results and comparison metrics */
int build_success_json(char *buf, size_t size, const int *pages, int n, int frames,
                       const result_t *results, int count, const comparison_t *cmp) {
    json_writer_t w;
    json_init(&w, buf, size);

    // Root object opening
    json_append(&w, "{\n  \"ok\": true,\n  \"reference\": [");
    for (int i = 0; i < n; i++) {
        json_append(w, "%d%s", pages[i], (i < n - 1) ? ", " : "");
    }
    json_append(&w, "],\n  \"frames\": %d,\n  \"results\": [\n", frames);

    // Loop through each algorithm result
    for (int i = 0; i < count; i++) {
        const result_t *r = &results[i];
        const char *algo_name = (r->algo >= 0 && r->algo < NUM_ALGOS) ? ALGO_NAMES[r->algo] : "UNKNOWN";

        json_append(&w, "    {\n");
        json_append(&w, "      \"algorithm\": \"%s\", \"total\": %d, \"hits\": %d, \"faults\": %d,\n",
                    algo_name, r->nsteps, r->hits, r->faults);
        json_append(&w, "      \"hit_ratio\": %.2f, \"fault_ratio\": %.2f,\n",
                    r->hit_ratio, r->fault_ratio);
        json_append(&w, "      \"steps\": [\n");

        // Loop through each step in the simulation
        for (int j = 0; j < r->nsteps; j++) {
            const step_t *s = &r->steps[j];
            json_append(&w, "        {\"page\": %d, \"frames\": [", s->page);

            // Write frames (EMPTY translates to null)
            for (int f = 0; f < frames; f++) {
                if (s->frames[f] == EMPTY) {
                    json_append(w, "null");
                } else {
                    json_append(w, "%d", s->frames[f]);
                }
                if (f < frames - 1) json_append(w, ", ");
            }
            json_append(&w, "], \"hit\": %s, \"replaced\": ", s->hit ? "true" : "false");

            // Write replaced page or null
            if (s->replaced == EMPTY) {
                json_append(w, "null");
            } else {
                json_append(w, "%d", s->replaced);
            }

            json_append(w, "}%s\n", (j < r->nsteps - 1) ? "," : "");
        }

        json_append(&w, "      \n    }%s\n", (i < count - 1) ? "," : "");
    }

    json_append(&w, "  ],\n  \"comparison\": {\"best\": [");

    // Write best algorithms list from comparison struct (indexes into results array)
    for (int i = 0; i < cmp->nbest; i++) {
        int res_idx = cmp->best[i];
        const char *best_algo_name = "UNKNOWN";
        if (res_idx >= 0 && res_idx < count) {
            algo_id_t aid = results[res_idx].algo;
            if (aid >= 0 && aid < NUM_ALGOS) {
                best_algo_name = ALGO_NAMES[aid];
            }
        }
        json_append(&w, "\"%s\"%s", best_algo_name, (i < cmp->nbest - 1) ? ", " : "");
    }

    json_append(&w, "], \"min_faults\": %d}\n", cmp->min_faults);
    json_append(&w, "}\n");

    if (w.error) {
        return -1;
    }
    return (int)w.offset;
}

/* Build the error JSON response string with proper message escaping */
int build_error_json(char *buf, size_t size, const char *message) {
    json_writer_t w;
    json_init(&w, buf, size);

    json_append(&w, "{\"ok\": false, \"error\": ");
    json_append_escaped_string(&w, message ? message : "Unknown error");
    json_append(&w, "}\n");

    if (w.error) {
        return -1;
    }
    return (int)w.offset;
}
