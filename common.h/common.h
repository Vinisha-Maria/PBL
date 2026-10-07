/* common.h - shared by UI, Core and Logger (Scheduler Simulator).
 * Owner: Team Leader (Vinisha). Do NOT change alone - agree changes in the group. */
#ifndef COMMON_H
#define COMMON_H

#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Three POSIX message queues connect the three processes */
#define Q_UI_TO_CORE  "/sched_ui_to_core"    /* UI   -> Core   : commands        */
#define Q_CORE_TO_UI  "/sched_core_to_ui"    /* Core -> UI     : state / errors  */
#define Q_CORE_TO_LOG "/sched_core_to_log"   /* Core -> Logger : log events      */

#define Q_MAXMSG   10
#define PAYLOAD_SZ 256

typedef enum {
    CMD_ADD = 1,    /* UI -> Core   payload = "<name> <burst>"       e.g. "P1 5" */
    CMD_ALGO,       /* UI -> Core   payload = "fcfs" | "sjf" | "rr"              */
    CMD_QUANTUM,    /* UI -> Core   payload = time slice for Round Robin, e.g. "2" */
    CMD_STEP,       /* UI -> Core   advance the simulation by one time unit      */
    CMD_RUN,        /* UI -> Core   run until all processes finish               */
    CMD_RESET,      /* UI -> Core   clear all processes and time                 */
    CMD_STATUS,     /* UI -> Core   send the current state                       */
    CMD_QUIT,       /* UI -> Core   shut everything down                         */
    STATE_UPDATE,   /* Core -> UI   payload = text snapshot                      */
    ERROR_MSG,      /* Core -> UI   payload = error description                  */
    LOG_EVENT,      /* Core -> Log  normal event                                 */
    LOG_ERROR,      /* Core -> Log  error event                                  */
    LOG_QUIT        /* Core -> Log  logger should stop                           */
} MsgType;

/* Rule: every command from the UI (except CMD_QUIT) gets exactly ONE reply
 * (STATE_UPDATE or ERROR_MSG). The UI relies on this for its "wait" command. */

typedef struct {
    int  type;                  /* MsgType */
    char payload[PAYLOAD_SZ];
    long timestamp;             /* seconds since epoch */
} Message;

static inline mqd_t open_queue(const char *name, int flags)
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg  = Q_MAXMSG;
    attr.mq_msgsize = sizeof(Message);
    return mq_open(name, flags | O_CREAT, 0666, &attr);
}

static inline void make_msg(Message *m, int type, const char *text)
{
    memset(m, 0, sizeof(*m));
    m->type = type;
    if (text) snprintf(m->payload, PAYLOAD_SZ, "%s", text);
    m->timestamp = (long)time(NULL);
}

#endif

