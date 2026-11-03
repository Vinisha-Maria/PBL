
/*
 * validation.c - checks the input of the Page Replacement Simulator.
 *
 * Three functions:
 *   parse_reference_string()  - turns "7 0 1, 2" into an int array
 *   validate_frames()         - checks the number of frames (1 to 10)
 *   validate_algorithm()      - turns "fifo"/"lru"/"optimal"/"all" into a mask
 *
 * Each function returns 0 if the input is ok, or -1 on error.
 * On error, the message is written into 'err'.
 */

#include <stdio.h>
#include <string.h>
#include "common.h"

/* Most characters of a bad token that we show in an error message. */
#define TOKEN_SHOW 20

/* Longest algorithm word we need to compare ("optimal" has 7 letters). */
#define ALGO_WORD_MAX 16

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Copy a fixed message into err. Safe if err is NULL or errsize is 0. */
static void set_message(char *err, size_t errsize, const char *msg)
{
    if (err == NULL || errsize == 0) {
        return;
    }
    snprintf(err, errsize, "%s", msg);
}

/* 1 if c is a white space character (space, tab, new line, ...). */
static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

/* 1 if c is a separator between pages: white space or a comma. */
static int is_separator(char c)
{
    return is_blank(c) || c == ',';
}

/* 1 if c is a digit '0'..'9'. */
static int is_digit_char(char c)
{
    return c >= '0' && c <= '9';
}

/* Change an upper case letter to lower case. Other characters stay. */
static char to_lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/*
 * Check that the token start[0..len-1] has only digits and read its value.
 * Returns 1 if the token is only digits, 0 if not.
 * The value is stored in *value. It never goes above limit + 1, so a very
 * long string of digits cannot overflow an int.
 */
static int read_number(const char *start, size_t len, int limit, int *value)
{
    int v = 0;
    size_t i;

    if (len == 0) {
        return 0;                       /* no digits at all */
    }
    for (i = 0; i < len; i++) {
        if (!is_digit_char(start[i])) {
            return 0;                   /* a bad character */
        }
        if (v <= limit) {               /* stop growing once too big */
            v = v * 10 + (start[i] - '0');
        }
    }
    *value = v;
    return 1;
}

/*
 * How many bytes of s[0..len-1] hold its first max_chars characters.
 * The text is UTF-8, so a character can use more than one byte. We never
 * cut in the middle of a character (bytes 10xxxxxx continue a character).
 */
static size_t cut_chars(const char *s, size_t len, int max_chars)
{
    size_t i = 0;
    int chars = 0;

    while (i < len) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {     /* start of a character */
            if (chars == max_chars) {
                break;
            }
            chars++;
        }
        i++;
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* parse_reference_string                                              */
/* ------------------------------------------------------------------ */

int parse_reference_string(const char *text, int *pages, int *n,
                           char *err, size_t errsize)
{
    const char *p;
    int count = 0;                      /* number of tokens seen so far */

    if (n != NULL) {
        *n = 0;
    }
    if (text == NULL) {
        text = "";                      /* NULL is the same as empty */
    }

    p = text;
    while (*p != '\0') {
        const char *start;
        size_t len;
        int value = 0;

        /* Skip spaces and commas between tokens. */
        while (*p != '\0' && is_separator(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* Find the end of this token. */
        start = p;
        while (*p != '\0' && !is_separator(*p)) {
            p++;
        }
        len = (size_t)(p - start);

        /* The token must be digits only and from 0 to 999. */
        if (!read_number(start, len, MAX_PAGE_NUMBER, &value) ||
            value > MAX_PAGE_NUMBER) {
            int show = (int)cut_chars(start, len, TOKEN_SHOW);
            if (err != NULL && errsize > 0) {
                snprintf(err, errsize,
                         "Invalid page '%.*s' at position %d. "
                         "Pages must be whole numbers from 0 to 999.",
                         show, start, count + 1);
            }
            return -1;
        }

        /* Store the page only if there is room in the array. */
        if (count < MAX_PAGES && pages != NULL) {
            pages[count] = value;
        }
        count++;
    }

    if (count == 0) {
        set_message(err, errsize, "Reference string is empty.");
        return -1;
    }
    if (count > MAX_PAGES) {
        set_message(err, errsize,
                    "Too many pages: the reference string can have "
                    "at most 100 pages.");
        return -1;
    }

    if (n != NULL) {
        *n = count;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* validate_frames                                                     */
/* ------------------------------------------------------------------ */

int validate_frames(const char *text, int *frames, char *err, size_t errsize)
{
    static const char *const msg =
        "Number of frames must be a whole number from 1 to 10.";
    const char *start;
    const char *end;
    int value = 0;

    if (text == NULL) {
        set_message(err, errsize, msg);
        return -1;
    }

    /* Ignore spaces before and after the number. */
    start = text;
    while (*start != '\0' && is_blank(*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && is_blank(*(end - 1))) {
        end--;
    }

    /* Only digits are allowed, and there must be at least one. */
    if (!read_number(start, (size_t)(end - start), MAX_FRAMES, &value) ||
        value < 1 || value > MAX_FRAMES) {
        set_message(err, errsize, msg);
        return -1;
    }

    if (frames != NULL) {
        *frames = value;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* validate_algorithm                                                  */
/* ------------------------------------------------------------------ */

int validate_algorithm(const char *text, int *mask, char *err, size_t errsize)
{
    const char *start;
    const char *end;
    char word[ALGO_WORD_MAX];
    size_t len;
    size_t i;
    int result = 0;

    /* A missing algorithm means all of them. */
    if (text == NULL) {
        if (mask != NULL) {
            *mask = ALGO_BIT(ALGO_FIFO) | ALGO_BIT(ALGO_LRU) |
                    ALGO_BIT(ALGO_OPTIMAL);
        }
        return 0;
    }

    /* Ignore spaces before and after the word. */
    start = text;
    while (*start != '\0' && is_blank(*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && is_blank(*(end - 1))) {
        end--;
    }
    len = (size_t)(end - start);

    /* Make a lower case copy. A longer word cannot be a valid name. */
    if (len < ALGO_WORD_MAX) {
        for (i = 0; i < len; i++) {
            word[i] = to_lower_char(start[i]);
        }
        word[len] = '\0';

        if (strcmp(word, "fifo") == 0) {
            result = ALGO_BIT(ALGO_FIFO);
        } else if (strcmp(word, "lru") == 0) {
            result = ALGO_BIT(ALGO_LRU);
        } else if (strcmp(word, "optimal") == 0) {
            result = ALGO_BIT(ALGO_OPTIMAL);
        } else if (strcmp(word, "all") == 0) {
            result = ALGO_BIT(ALGO_FIFO) | ALGO_BIT(ALGO_LRU) |
                     ALGO_BIT(ALGO_OPTIMAL);
        }
    }

    if (result == 0) {
        /* Show the original text, cut to its first 20 characters. */
        int show = (int)cut_chars(text, strlen(text), TOKEN_SHOW);
        if (err != NULL && errsize > 0) {
            snprintf(err, errsize,
                     "Unknown algorithm '%.*s'. "
                     "Choose fifo, lru, optimal or all.", show, text);
        }
        return -1;
    }

    if (mask != NULL) {
        *mask = result;
    }
    return 0;
}
