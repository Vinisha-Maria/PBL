/*
 * server.c - the web server and the main() of the Page Replacement Simulator.
 *
 * What it does:
 *   - listens on a TCP port (5000, or the number in the PORT variable)
 *   - handles ONE connection at a time (accept, answer, close)
 *   - GET /                   -> static/index.html
 *   - GET /static/style.css   -> static/style.css
 *   - GET /static/app.js      -> static/app.js
 *   - POST /api/simulate      -> checks the input, runs the algorithms,
 *                                sends the result as JSON
 *
 * Compile:
 *   gcc -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
 *       src/server.c src/validation.c src/stats.c ... -o simulator
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>        /* strncasecmp */
#include <time.h>           /* nanosleep */
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>       /* struct timeval */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

#define DEFAULT_PORT      5000
#define LISTEN_BACKLOG    16
#define HEADER_MAX        16384          /* most bytes we read for the headers */
#define BODY_MAX          8192           /* biggest request body we accept */
#define FILE_MAX          (1024 * 1024)  /* biggest static file we send */
#define RECV_TIMEOUT_SEC  5              /* a slow client is dropped after this */
#define REQUEST_TIME_MAX  10             /* most seconds to read one whole request */
#define LINGER_TIME_MAX   1.0            /* most seconds to wait after our answer */
#define LINGER_USEC       300000         /* one wait while we read away the rest */
#define DRAIN_MAX         65536          /* most bytes we throw away after an answer */
#define MAX_METHOD        15
#define MAX_PATH          255

/* Messages. The JSON ones must be exactly as in the specification. */
#define MSG_NOT_JSON   "Request must be JSON."
#define MSG_INTERNAL   "Internal error."
#define MSG_NOT_FOUND  "Not found"
#define MSG_BAD_METHOD "Method not allowed"

/* Results of get_content_length(). Real lengths are 0 or more. */
#define CL_MISSING (-2)
#define CL_INVALID (-1)

/* ------------------------------------------------------------------ */
/* Static files: only these exact paths can be asked for               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *url;            /* path in the request */
    const char *file;           /* file name on disk (in the current folder) */
    const char *content_type;
} static_file_t;

static const static_file_t STATIC_FILES[] = {
    { "/",                  "static/index.html", "text/html; charset=utf-8" },
    { "/static/style.css",  "static/style.css",  "text/css" },
    { "/static/app.js",     "static/app.js",     "application/javascript" }
};
#define NUM_STATIC_FILES ((int)(sizeof STATIC_FILES / sizeof STATIC_FILES[0]))

#define API_PATH "/api/simulate"

/* Big buffers are static so that the stack stays small. */
static char g_json[JSON_BUF_SIZE];      /* JSON answer */
static char g_file[FILE_MAX];           /* content of a static file */
static double g_deadline;               /* time (seconds) when we give up on this request */

/* Seconds from a clock that never goes back. */
static double now_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------ */
/* Sending an answer                                                   */
/* ------------------------------------------------------------------ */

/* Send all len bytes. Handles partial writes. Returns 0 ok, -1 error. */
static int send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;               /* interrupted, try again */
            }
            return -1;                  /* real error (or timeout) */
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/*
 * Send a complete HTTP response:
 *   HTTP/1.1 <code> <text>, Content-Type, Content-Length, Connection: close,
 *   an empty line, and then the body.
 * Returns 0 if everything was sent, -1 if not.
 */
static int send_response(int fd, int status_code, const char *status_text,
                         const char *content_type, const char *body,
                         size_t body_length)
{
    char header[512];
    int len;

    len = snprintf(header, sizeof header,
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   status_code, status_text, content_type, body_length);
    if (len < 0 || (size_t)len >= sizeof header) {
        return -1;
    }
    if (send_all(fd, header, (size_t)len) < 0) {
        return -1;
    }
    if (body_length > 0 && send_all(fd, body, body_length) < 0) {
        return -1;
    }
    return 0;
}

/* Send a short plain text answer. Returns the status code (for the log). */
static int send_text(int fd, int code, const char *status_text, const char *msg)
{
    send_response(fd, code, status_text, "text/plain; charset=utf-8",
                  msg, strlen(msg));
    return code;
}

/* Send {"ok": false, "error": "..."} as JSON. Returns the status code. */
static int send_json_error(int fd, int code, const char *status_text,
                           const char *message)
{
    static const char fallback[] = "{\"ok\": false, \"error\": \"Internal error.\"}";
    int len = build_error_json(g_json, sizeof g_json, message);

    if (len < 0) {
        send_response(fd, code, status_text, "application/json",
                      fallback, sizeof fallback - 1);
    } else {
        send_response(fd, code, status_text, "application/json",
                      g_json, (size_t)len);
    }
    return code;
}

/* Send one of the three static files. Returns the status code. */
static int serve_file(int fd, const static_file_t *sf)
{
    FILE *f = fopen(sf->file, "rb");
    size_t n;

    if (f == NULL) {
        return send_text(fd, 404, "Not Found", MSG_NOT_FOUND);
    }

    n = fread(g_file, 1, sizeof g_file, f);
    if (ferror(f) || (n == sizeof g_file && fgetc(f) != EOF)) {
        /* read error, or the file is bigger than our buffer */
        fclose(f);
        return send_text(fd, 500, "Internal Server Error", MSG_INTERNAL);
    }
    fclose(f);

    send_response(fd, 200, "OK", sf->content_type, g_file, n);
    return 200;
}

/* ------------------------------------------------------------------ */
/* Reading the JSON body                                               */
/* ------------------------------------------------------------------ */

static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/*
 * Find "key" in the JSON text and copy its value into out.
 *   - value starting with a double quote: copy up to the next double quote
 *     (a backslash before a character is removed, so \" is part of the text;
 *      \n, \t and \r become a real new line, tab and carriage return)
 *   - any other value: copy up to a comma, '}' or white space
 * Returns 1 if the key was found, 0 if not. Never writes more than outsize.
 */
static int json_get_value(const char *body, const char *key,
                          char *out, size_t outsize)
{
    char pattern[64];
    const char *p;
    const char *q;
    size_t used = 0;
    int n;

    if (out == NULL || outsize == 0) {
        return 0;
    }
    out[0] = '\0';
    if (body == NULL || key == NULL) {
        return 0;
    }

    n = snprintf(pattern, sizeof pattern, "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof pattern) {
        return 0;
    }

    /* Find "key" followed by optional spaces and a colon. */
    p = body;
    for (;;) {
        const char *found = strstr(p, pattern);
        if (found == NULL) {
            return 0;
        }
        q = found + n;
        while (is_space_char(*q)) {
            q++;
        }
        if (*q == ':') {
            q++;
            break;
        }
        p = found + 1;          /* not a key (maybe text inside a value) */
    }
    while (is_space_char(*q)) {
        q++;
    }

    if (*q == '"') {
        /* A text value. */
        q++;
        while (*q != '\0' && *q != '"') {
            char c = *q;
            if (c == '\\' && q[1] != '\0') {
                q++;
                if (*q == 'n') {
                    c = '\n';
                } else if (*q == 't') {
                    c = '\t';
                } else if (*q == 'r') {
                    c = '\r';
                } else {
                    c = *q;     /* \" or \\ or \/ : keep the character */
                }
            }
            if (used + 1 < outsize) {
                out[used++] = c;
            }
            q++;
        }
    } else {
        /* A number, true, false, null, ... */
        while (*q != '\0' && *q != ',' && *q != '}' && !is_space_char(*q)) {
            if (used + 1 < outsize) {
                out[used++] = *q;
            }
            q++;
        }
    }
    out[used] = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* POST /api/simulate                                                  */
/* ------------------------------------------------------------------ */

/* Check the input, run the algorithms and send the answer. Returns status. */
static int handle_simulate(int fd, const char *body)
{
    static result_t results[NUM_ALGOS];
    static char reference_text[BODY_MAX + 1];
    static char frames_text[BODY_MAX + 1];
    static char algo_text[BODY_MAX + 1];

    int pages[MAX_PAGES];
    int n = 0;
    int frames = 0;
    int mask = 0;
    int count = 0;
    int len;
    int i;
    char err[256];
    comparison_t cmp;

    /* The body must be a JSON object. */
    if (strchr(body, '{') == NULL) {
        return send_json_error(fd, 400, "Bad Request", MSG_NOT_JSON);
    }

    /* A missing key gives NULL, and the checking functions know what to do. */
    if (!json_get_value(body, "reference", reference_text, sizeof reference_text)) {
        reference_text[0] = '\0';
    }
    const char *frames_arg = json_get_value(body, "frames", frames_text,
                                            sizeof frames_text) ? frames_text : NULL;
    const char *algo_arg = json_get_value(body, "algorithm", algo_text,
                                          sizeof algo_text) ? algo_text : NULL;

    /* Check in this order: reference string, frames, algorithm. */
    if (parse_reference_string(reference_text, pages, &n, err, sizeof err) != 0) {
        return send_json_error(fd, 400, "Bad Request", err);
    }
    if (validate_frames(frames_arg, &frames, err, sizeof err) != 0) {
        return send_json_error(fd, 400, "Bad Request", err);
    }
    if (validate_algorithm(algo_arg, &mask, err, sizeof err) != 0) {
        return send_json_error(fd, 400, "Bad Request", err);
    }

    /* Run the algorithms (the worker processes). */
    if (ipc_run_algorithms(pages, n, frames, mask, results, &count,
                           err, sizeof err) != 0 ||
        count < 1 || count > NUM_ALGOS) {
        fprintf(stderr, "ipc_run_algorithms failed\n");
        return send_json_error(fd, 500, "Internal Server Error", MSG_INTERNAL);
    }

    /* Hits, faults and ratios, then the comparison. */
    for (i = 0; i < count; i++) {
        summarize(&results[i]);
    }
    compare(results, count, &cmp);

    len = build_success_json(g_json, sizeof g_json, pages, n, frames,
                             results, count, &cmp);
    if (len < 0) {
        return send_json_error(fd, 500, "Internal Server Error", MSG_INTERNAL);
    }

    send_response(fd, 200, "OK", "application/json", g_json, (size_t)len);
    return 200;
}

/* ------------------------------------------------------------------ */
/* Reading a request                                                   */
/* ------------------------------------------------------------------ */

/* Return the index after the first "\r\n\r\n" (looking from 'from'), or 0. */
static size_t find_header_end(const char *buf, size_t total, size_t from)
{
    size_t i;

    for (i = from; i + 4 <= total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    return 0;
}

/*
 * Read from the socket until the end of the headers. At most HEADER_MAX
 * bytes are read (buf must have room for HEADER_MAX + 1).
 * Returns 0 if ok, -1 if the request cannot be read.
 */
static int read_headers(int fd, char *buf, size_t *total_out, size_t *end_out)
{
    size_t total = 0;

    for (;;) {
        size_t from = (total > 3) ? total - 3 : 0;
        size_t end;
        ssize_t n;

        if (total >= HEADER_MAX || now_seconds() > g_deadline) {
            return -1;                  /* too big, or too slow */
        }
        n = recv(fd, buf + total, HEADER_MAX - total, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;                  /* error or 5 second timeout */
        }
        if (n == 0) {
            return -1;                  /* client closed the connection */
        }
        total += (size_t)n;
        buf[total] = '\0';

        end = find_header_end(buf, total, from);
        if (end > 0) {
            *total_out = total;
            *end_out = end;
            return 0;
        }
    }
}

/* Read a number from v up to end. Returns CL_INVALID if it is not valid. */
static long parse_length(const char *v, const char *end)
{
    long value = 0;
    int digits = 0;

    while (v < end && *v >= '0' && *v <= '9') {
        if (value <= 100000000L) {      /* stop growing: no overflow */
            value = value * 10 + (*v - '0');
        }
        digits++;
        v++;
    }
    while (v < end && (*v == ' ' || *v == '\t')) {
        v++;
    }
    if (digits == 0 || v != end) {
        return CL_INVALID;              /* empty, negative, or has letters */
    }
    return value;
}

/*
 * Look for the Content-Length header (the name can be in any case).
 * Returns the length, CL_MISSING or CL_INVALID.
 */
static long get_content_length(const char *req, size_t header_end)
{
    const char *limit = req + header_end;
    const char *p = strstr(req, "\r\n");    /* skip the first line */

    if (p == NULL) {
        return CL_MISSING;
    }
    p += 2;

    while (p < limit) {
        const char *eol = strstr(p, "\r\n");
        size_t len;

        if (eol == NULL || eol > limit) {
            break;
        }
        len = (size_t)(eol - p);
        if (len == 0) {
            break;                          /* empty line: end of headers */
        }
        if (len > 14 && strncasecmp(p, "content-length", 14) == 0) {
            const char *v = p + 14;
            while (v < eol && (*v == ' ' || *v == '\t')) {
                v++;
            }
            if (v < eol && *v == ':') {
                v++;
                while (v < eol && (*v == ' ' || *v == '\t')) {
                    v++;
                }
                return parse_length(v, eol);
            }
        }
        p = eol + 2;
    }
    return CL_MISSING;
}

/*
 * Get exactly 'length' body bytes into body (room for length + 1).
 * Some of them may already be in req, after the headers.
 * Returns 0 if ok, -1 if the client did not send them.
 */
static int read_body(int fd, const char *req, size_t total, size_t header_end,
                     size_t length, char *body)
{
    size_t have = total - header_end;
    size_t got;

    if (have > length) {
        have = length;                      /* ignore extra bytes */
    }
    memcpy(body, req + header_end, have);
    got = have;

    while (got < length) {
        ssize_t n;

        if (now_seconds() > g_deadline) {
            return -1;                      /* the client is too slow */
        }
        n = recv(fd, body + got, length - got, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        got += (size_t)n;
    }
    body[length] = '\0';
    return 0;
}

/*
 * Close a connection politely: say "we are done" (FIN), then read away what
 * the client still sends, for a short time. If we closed with unread data,
 * the client could get "connection reset" and lose our answer.
 */
static void close_connection(int fd)
{
    char tmp[4096];
    size_t total = 0;
    struct timeval tv;
    double end = now_seconds() + LINGER_TIME_MAX;

    tv.tv_sec = 0;
    tv.tv_usec = LINGER_USEC;
    if (shutdown(fd, SHUT_WR) == 0 &&
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0) {
        while (total < DRAIN_MAX && now_seconds() < end) {
            ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n <= 0) {
                break;                      /* client closed, or nothing more */
            }
            total += (size_t)n;
        }
    }
    /* (If shutdown failed, the client is already gone: just close.) */
    if (close(fd) < 0) {
        perror("close");
    }
}

/* Copy up to len bytes (and at most size-1) and change odd characters to '?'. */
static void copy_clean(char *dst, size_t size, const char *src, size_t len)
{
    size_t i;

    if (len > size - 1) {
        len = size - 1;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 0x20 || c >= 0x7f) ? '?' : (char)c;
    }
    dst[len] = '\0';
}

/* ------------------------------------------------------------------ */
/* One connection                                                      */
/* ------------------------------------------------------------------ */

static void handle_connection(int fd)
{
    static char req[HEADER_MAX + 1];
    static char body[BODY_MAX + 1];

    char method[MAX_METHOD + 1];
    char path[MAX_PATH + 1];
    size_t total = 0;
    size_t header_end = 0;
    size_t line_len;
    const char *eol;
    const char *sp1;
    const char *sp2;
    const char *path_end;
    int status = 0;
    int i;

    g_deadline = now_seconds() + REQUEST_TIME_MAX;

    /* Read the headers. If this fails we just close the connection. */
    if (read_headers(fd, req, &total, &header_end) < 0) {
        return;
    }

    /* First line: METHOD PATH VERSION */
    eol = strstr(req, "\r\n");
    if (eol == NULL) {
        return;
    }
    line_len = (size_t)(eol - req);
    sp1 = memchr(req, ' ', line_len);
    if (sp1 == NULL || sp1 == req || (size_t)(sp1 - req) > MAX_METHOD) {
        return;
    }
    sp2 = memchr(sp1 + 1, ' ', line_len - (size_t)(sp1 + 1 - req));
    path_end = (sp2 != NULL) ? sp2 : eol;

    copy_clean(method, sizeof method, req, (size_t)(sp1 - req));
    copy_clean(path, sizeof path, sp1 + 1, (size_t)(path_end - (sp1 + 1)));

    if (strcmp(path, API_PATH) == 0) {
        /* ---- the API ---- */
        if (strcmp(method, "POST") != 0) {
            status = send_text(fd, 405, "Method Not Allowed", MSG_BAD_METHOD);
        } else {
            long cl = get_content_length(req, header_end);

            if (cl < 0 || cl > BODY_MAX) {
                /* missing, negative, bad or too big */
                status = send_json_error(fd, 400, "Bad Request", MSG_NOT_JSON);
            } else if (read_body(fd, req, total, header_end,
                                 (size_t)cl, body) < 0) {
                fprintf(stderr, "%s %s (incomplete request)\n", method, path);
                return;
            } else {
                status = handle_simulate(fd, body);
            }
        }
    } else {
        /* ---- static files, or an unknown path ---- */
        const static_file_t *sf = NULL;

        for (i = 0; i < NUM_STATIC_FILES; i++) {
            if (strcmp(path, STATIC_FILES[i].url) == 0) {
                sf = &STATIC_FILES[i];
                break;
            }
        }
        if (sf == NULL) {
            status = send_text(fd, 404, "Not Found", MSG_NOT_FOUND);
        } else if (strcmp(method, "GET") != 0) {
            status = send_text(fd, 405, "Method Not Allowed", MSG_BAD_METHOD);
        } else {
            status = serve_file(fd, sf);
        }
    }

    fprintf(stderr, "%s %s %d\n", method, path, status);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

/* Port from the PORT variable. Returns the port, or -1 if it is not valid. */
static int read_port(void)
{
    const char *env = getenv("PORT");
    const char *p;
    long value = 0;

    if (env == NULL || *env == '\0') {
        return DEFAULT_PORT;
    }
    for (p = env; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        if (value <= 65535) {
            value = value * 10 + (*p - '0');
        }
    }
    if (value < 1 || value > 65535) {
        return -1;
    }
    return (int)value;
}

int main(void)
{
    int port;
    int listen_fd;
    int yes = 1;
    struct sockaddr_in addr;

    /* A client that disconnects must not kill the server. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    port = read_port();
    if (port < 0) {
        fprintf(stderr, "PORT must be a number from 1 to 65535\n");
        return 1;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(listen_fd);
        return 1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);       /* 0.0.0.0 */
    addr.sin_port = htons((unsigned short)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("Listening on http://0.0.0.0:%d\n", port);
    fflush(stdout);

    /* Serve one connection at a time, for ever. */
    for (;;) {
        struct timeval tv;
        int fd = accept(listen_fd, NULL, NULL);

        if (fd < 0) {
            if (errno != EINTR) {
                struct timespec pause = { 0, 10000000L };   /* 10 ms */
                perror("accept");
                nanosleep(&pause, NULL);    /* do not spin on a hard error */
            }
            continue;
        }

        /* A client that stops half way must not block us for ever. */
        tv.tv_sec = RECV_TIMEOUT_SEC;
        tv.tv_usec = 0;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0 ||
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0) {
            perror("setsockopt timeout");
            close(fd);
            continue;
        }

        handle_connection(fd);
        close_connection(fd);
    }

    /* not reached */
    return 0;
}

