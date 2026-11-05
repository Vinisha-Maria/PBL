/*
 * json.c - writes the simulator answer as JSON text for the browser.
 *
 * Two public functions:
 *   build_success_json : the full answer (reference, frames, results, comparison)
 *   build_error_json   : {"ok": false, "error": "<message>"}
 *
 * Both return the number of characters written (without the final '\0'),
 * or -1 if the buffer is too small. They never write outside the buffer.
 *
 * All writing goes through one small helper (jb_add) that appends text,
 * keeps the position, and remembers if the buffer became too small.
 * No global or static variables are used.
 */

#include <stdarg.h>
#include <stdio.h>
#include "common.h"

/* A buffer we append to. */
typedef struct {
    char  *buf;      /* where the text is written */
    size_t size;     /* total size of buf */
    size_t pos;      /* number of characters written so far */
    int    failed;   /* 1 = buffer too small (or bad input), stop writing */
} jbuf_t;

/* Prepare the buffer for writing. */
static void jb_init(jbuf_t *j, char *buf, size_t size)
{
    j->buf = buf;
    j->size = size;
    j->pos = 0;
    j->failed = 0;
    if (buf == NULL || size == 0)
        j->failed = 1;          /* nowhere to write */
    else
        buf[0] = '\0';
}

/* Append formatted text. If it does not fit (we also need room for the
 * final '\0'), mark the buffer as failed. Nothing is written after that. */
static void jb_add(jbuf_t *j, const char *fmt, ...)
{
    va_list ap;
    int written;
    size_t room;

    if (j->failed)
        return;

    room = j->size - j->pos;    /* pos is always smaller than size here */
    va_start(ap, fmt);
    written = vsnprintf(j->buf + j->pos, room, fmt, ap);
    va_end(ap);

    if (written < 0 || (size_t)written >= room) {
        j->failed = 1;          /* too small: the text was cut */
        return;
    }
    j->pos += (size_t)written;
}

/* Finish: return the length, or -1 on failure (and leave an empty string). */
static int jb_finish(jbuf_t *j)
{
    if (j->failed || j->pos > 2147483647u) {
        if (j->buf != NULL && j->size > 0)
            j->buf[0] = '\0';
        return -1;
    }
    return (int)j->pos;
}

/* Append a number, or the word null when the value is EMPTY. */
static void jb_int_or_null(jbuf_t *j, int value)
{
    if (value == EMPTY)
        jb_add(j, "null");
    else
        jb_add(j, "%d", value);
}

/* Append a text as a JSON string (with quotes).
 * Escapes: double quote, backslash, and control characters. */
static void jb_string(jbuf_t *j, const char *s)
{
    jb_add(j, "\"");
    for (; s != NULL && *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  jb_add(j, "\\\""); break;
        case '\\': jb_add(j, "\\\\"); break;
        case '\n': jb_add(j, "\\n");  break;
        case '\r': jb_add(j, "\\r");  break;
        case '\t': jb_add(j, "\\t");  break;
        case '\b': jb_add(j, "\\b");  break;
        case '\f': jb_add(j, "\\f");  break;
        default:
            if (c < 0x20)
                jb_add(j, "\\u%04x", (unsigned)c);  /* other control char */
            else
                jb_add(j, "%c", (char)c);           /* normal character */
            break;
        }
    }
    jb_add(j, "\"");
}

/* Return the name of an algorithm, or NULL if the number is not valid. */
static const char *algo_name(int algo)
{
    if (algo < 0 || algo >= NUM_ALGOS)
        return NULL;
    return ALGO_NAMES[algo];
}

/* Write the frames array of one step: exactly 'frames' entries. */
static void jb_frames(jbuf_t *j, const step_t *st, int frames)
{
    jb_add(j, "[");
    for (int f = 0; f < frames; f++) {
        if (f > 0)
            jb_add(j, ", ");
        jb_int_or_null(j, st->frames[f]);
    }
    jb_add(j, "]");
}

/* Write one result object (one algorithm). */
static void jb_result(jbuf_t *j, const result_t *r, int frames)
{
    const char *name = algo_name((int)r->algo);

    if (name == NULL || r->nsteps < 0 || r->nsteps > MAX_PAGES) {
        j->failed = 1;          /* bad data: do not write wrong JSON */
        return;
    }

    jb_add(j, "    {\n");
    jb_add(j, "      \"algorithm\": ");
    jb_string(j, name);
    jb_add(j, ", \"total\": %d, \"hits\": %d, \"faults\": %d,\n",
           r->nsteps, r->hits, r->faults);
    jb_add(j, "      \"hit_ratio\": %.2f, \"fault_ratio\": %.2f,\n",
           r->hit_ratio, r->fault_ratio);
    jb_add(j, "      \"steps\": [\n");

    for (int s = 0; s < r->nsteps; s++) {
        const step_t *st = &r->steps[s];

        jb_add(j, "        {\"page\": %d, \"frames\": ", st->page);
        jb_frames(j, st, frames);
        jb_add(j, ", \"hit\": %s, \"replaced\": ", st->hit ? "true" : "false");
        jb_int_or_null(j, st->replaced);
        jb_add(j, "}%s\n", (s + 1 < r->nsteps) ? "," : "");
    }

    jb_add(j, "      ]\n");
    jb_add(j, "    }");
}

/* Build the success JSON: reference, frames, results and comparison. */
int build_success_json(char *buf, size_t size, const int *pages, int n, int frames,
                       const result_t *results, int count, const comparison_t *cmp)
{
    jbuf_t jb;
    jbuf_t *j = &jb;

    jb_init(j, buf, size);

    /* Check the sizes so we never read outside the arrays. */
    if (pages == NULL || results == NULL || cmp == NULL ||
        n < 0 || n > MAX_PAGES ||
        frames < 1 || frames > MAX_FRAMES ||
        count < 0 || count > NUM_ALGOS ||
        cmp->nbest < 0 || cmp->nbest > NUM_ALGOS)
        j->failed = 1;

    jb_add(j, "{\n");
    jb_add(j, "  \"ok\": true,\n");

    /* "reference": the page list */
    jb_add(j, "  \"reference\": [");
    for (int i = 0; !j->failed && i < n; i++)
        jb_add(j, "%s%d", (i > 0) ? ", " : "", pages[i]);
    jb_add(j, "],\n");

    /* "frames": number of frames */
    jb_add(j, "  \"frames\": %d,\n", frames);

    /* "results": one object per algorithm, same order as the array */
    jb_add(j, "  \"results\": [\n");
    for (int i = 0; !j->failed && i < count; i++) {
        jb_result(j, &results[i], frames);
        jb_add(j, "%s\n", (i + 1 < count) ? "," : "");
    }
    jb_add(j, "  ],\n");

    /* "comparison": names of the best algorithms and the fewest faults */
    jb_add(j, "  \"comparison\": {\"best\": [");
    for (int i = 0; !j->failed && i < cmp->nbest; i++) {
        int idx = cmp->best[i];              /* index into the results array */
        const char *name = NULL;

        if (idx >= 0 && idx < count)
            name = algo_name((int)results[idx].algo);
        if (name == NULL) {
            j->failed = 1;
            break;
        }
        if (i > 0)
            jb_add(j, ", ");
        jb_string(j, name);
    }
    jb_add(j, "], \"min_faults\": %d}\n", cmp->min_faults);
    jb_add(j, "}\n");

    return jb_finish(j);
}

/* Build the error JSON: {"ok": false, "error": "<message>"} */
int build_error_json(char *buf, size_t size, const char *message)
{
    jbuf_t jb;
    jbuf_t *j = &jb;

    jb_init(j, buf, size);

    jb_add(j, "{\"ok\": false, \"error\": ");
    jb_string(j, message != NULL ? message : "");
    jb_add(j, "}\n");

    return jb_finish(j);
}
