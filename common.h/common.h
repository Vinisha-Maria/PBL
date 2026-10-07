/*
 * common.h - shared contract for the OS process scheduler simulator
 *
 * Owner: Vinisha (Team Leader + IPC)
 * Used by: UI (Rian), Core (Rajath), Logger (Student 3), ipc wrapper
 *
 * RULE: change this file only by agreement with the whole team.
 *       Any change here means everyone must recompile.
 *
 * IPC technique: POSIX message queues (Linux only, link with -lrt).
 *
 *   UI  --/sched_cmd-->  Core  --/sched_log-->  Logger
 *   UI  <--/sched_resp-- Core
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Queue names (must start with '/')                                   */
/* ------------------------------------------------------------------ */
#define QUEUE_CMD   "/sched_cmd"    /* UI   -> Core   */
#define QUEUE_RESP  "/sched_resp"   /* Core -> UI     */
#define QUEUE_LOG   "/sched_log"    /* Core -> Logger */

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */
#define MAX_NAME_LEN     16     /* process name incl. '\0', e.g. "P1"  */
#define MAX_TEXT_LEN     256    /* status / error / log text incl. '\0'*/
#define MAX_PROCESSES    64     /* size of the Core process table      */
#define MAX_BURST        1000   /* largest valid burst time            */
#define MAX_QUANTUM      100    /* largest valid RR quantum            */
#define DEFAULT_QUANTUM  2

/*
 * Queue depth. Linux lets unprivileged users set up to 10
 * (see /proc/sys/fs/mqueue/msg_max). Do not raise without checking.
 */
#define MQ_MAX_MESSAGES  10
#define MQ_PRIORITY      0      /* all messages equal priority (FIFO)  */

/* ------------------------------------------------------------------ */
/* Scheduling algorithms                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    ALGO_FCFS = 0,
    ALGO_SJF  = 1,
    ALGO_RR   = 2
} algo_t;

/* ------------------------------------------------------------------ */
/* Message types                                                       */
/* ------------------------------------------------------------------ */
typedef enum {
    /* UI -> Core  (QUEUE_CMD) */
    CMD_ADD     = 1,    /* payload.add      : "add P1 5"   */
    CMD_ALGO    = 2,    /* payload.algo     : "algo rr"    */
    CMD_QUANTUM = 3,    /* payload.quantum  : "quantum 2"  */
    CMD_STEP    = 4,    /* no payload                      */
    CMD_RUN     = 5,    /* no payload                      */
    CMD_STATUS  = 6,    /* no payload                      */
    CMD_RESET   = 7,    /* no payload                      */
    CMD_QUIT    = 8,    /* no payload                      */

    /* Core -> UI  (QUEUE_RESP) */
    RESP_STATE  = 20,   /* payload.text : ready queue / timeline line */
    RESP_ERROR  = 21,   /* payload.text : error message               */
    RESP_END    = 22,   /* no payload   : last reply to a command     */

    /* Core -> Logger  (QUEUE_LOG) */
    EVT_START   = 40,   /* payload.event.proc                          */
    EVT_SWITCH  = 41,   /* payload.event.proc (from), .next (to)       */
    EVT_FINISH  = 42,   /* payload.event.proc, .waiting_time           */
    EVT_ERROR   = 43,   /* payload.text -> errors.log                  */
    EVT_STATS   = 44,   /* payload.text : final statistics line        */
    EVT_QUIT    = 45    /* no payload   : Logger flushes and exits     */
} msg_type_t;

/* ------------------------------------------------------------------ */
/* Message format (one struct for all three queues)                    */
/* ------------------------------------------------------------------ */
typedef struct {
    int32_t type;               /* a msg_type_t value                  */
    int32_t seq;                /* sender's counter, for ordering/debug*/

    union {
        struct {                /* CMD_ADD */
            char    name[MAX_NAME_LEN];
            int32_t burst;
        } add;

        struct {                /* CMD_ALGO */
            int32_t id;         /* an algo_t value */
        } algo;

        struct {                /* CMD_QUANTUM */
            int32_t value;
        } quantum;

        struct {                /* EVT_START / EVT_SWITCH / EVT_FINISH */
            char    proc[MAX_NAME_LEN];
            char    next[MAX_NAME_LEN];   /* EVT_SWITCH only */
            int32_t time;                 /* simulated clock */
            int32_t waiting_time;         /* EVT_FINISH only */
        } event;

        char text[MAX_TEXT_LEN];          /* RESP_*, EVT_ERROR, EVT_STATS */
    } payload;
} message_t;

#define MSG_SIZE ((long)sizeof(message_t))

/* Catch accidental growth beyond Linux's default 8192-byte limit. */
_Static_assert(sizeof(message_t) <= 8192, "message_t too large for mq_msgsize");

/* ------------------------------------------------------------------ */
/* Log files (Logger only)                                             */
/* ------------------------------------------------------------------ */
#define LOG_FILE_MAIN    "simulator.log"
#define LOG_FILE_ERRORS  "errors.log"

#endif /* COMMON_H */
