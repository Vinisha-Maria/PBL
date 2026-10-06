#include "ipc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <mqueue.h>
#include <unistd.h>
#include <errno.h>

#define USE_POSIX_MQ 1
#else
#define USE_POSIX_MQ 0
#endif

#if USE_POSIX_MQ

static mqd_t mq_in = (mqd_t)-1;
static mqd_t mq_log = (mqd_t)-1;
static mqd_t mq_resp = (mqd_t)-1;

bool ipc_core_init(IpcContext *ctx) {
    if (!ctx) return false;

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(IpcMessage);
    attr.mq_curmsgs = 0;

    // Unlink old queues if any
    mq_unlink(UI_TO_CORE_QUEUE);
    mq_unlink(CORE_TO_LOGGER_QUEUE);
    mq_unlink(CORE_TO_UI_QUEUE);

    // Open/Create UI -> Core queue (read-only for Core)
    mq_in = mq_open(UI_TO_CORE_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq_in == (mqd_t)-1) {
        perror("[IPC Error] Failed to create UI_TO_CORE_QUEUE");
        return false;
    }

    // Open/Create Core -> Logger queue (write-only for Core)
    mq_log = mq_open(CORE_TO_LOGGER_QUEUE, O_CREAT | O_WRONLY, 0666, &attr);
    if (mq_log == (mqd_t)-1) {
        perror("[IPC Error] Failed to create CORE_TO_LOGGER_QUEUE");
        mq_close(mq_in);
        return false;
    }

    // Open/Create Core -> UI response queue (write-only for Core)
    mq_resp = mq_open(CORE_TO_UI_QUEUE, O_CREAT | O_WRONLY, 0666, &attr);
    if (mq_resp == (mqd_t)-1) {
        perror("[IPC Error] Failed to create CORE_TO_UI_QUEUE");
        mq_close(mq_in);
        mq_close(mq_log);
        return false;
    }

    ctx->mq_in_desc = (int)mq_in;
    ctx->mq_log_desc = (int)mq_log;
    ctx->mq_resp_desc = (int)mq_resp;
    ctx->initialized = true;

    printf("[IPC Core] POSIX Message Queues initialized successfully.\n");
    printf("           Queue In   : %s\n", UI_TO_CORE_QUEUE);
    printf("           Queue Log  : %s\n", CORE_TO_LOGGER_QUEUE);
    printf("           Queue Resp : %s\n", CORE_TO_UI_QUEUE);
    return true;
}

bool ipc_core_receive_command(IpcContext *ctx, IpcMessage *out_msg) {
    if (!ctx || !ctx->initialized || !out_msg) return false;
    ssize_t bytes_read = mq_receive(mq_in, (char *)out_msg, sizeof(IpcMessage), NULL);
    if (bytes_read < 0) {
        perror("[IPC Error] mq_receive failed");
        return false;
    }
    return true;
}

bool ipc_core_send_log(IpcContext *ctx, int cmd_id, const char *log_text, int status_code, double exec_time_us) {
    if (!ctx || !ctx->initialized) return false;
    IpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = MSG_TYPE_LOG;
    msg.command_id = cmd_id;
    msg.status_code = status_code;
    msg.execution_time_us = exec_time_us;
    strncpy(msg.payload, log_text, sizeof(msg.payload) - 1);

    if (mq_send(mq_log, (const char *)&msg, sizeof(IpcMessage), 0) < 0) {
        perror("[IPC Error] Failed to send log to Logger queue");
        return false;
    }
    return true;
}

bool ipc_core_send_response(IpcContext *ctx, int cmd_id, const char *resp_text, int status_code, double exec_time_us) {
    if (!ctx || !ctx->initialized) return false;
    IpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = MSG_TYPE_RESP;
    msg.command_id = cmd_id;
    msg.status_code = status_code;
    msg.execution_time_us = exec_time_us;
    strncpy(msg.payload, resp_text, sizeof(msg.payload) - 1);

    if (mq_send(mq_resp, (const char *)&msg, sizeof(IpcMessage), 0) < 0) {
        perror("[IPC Error] Failed to send response to UI queue");
        return false;
    }
    return true;
}

void ipc_core_cleanup(IpcContext *ctx) {
    if (!ctx || !ctx->initialized) return;
    if (mq_in != (mqd_t)-1) { mq_close(mq_in); mq_unlink(UI_TO_CORE_QUEUE); }
    if (mq_log != (mqd_t)-1) { mq_close(mq_log); mq_unlink(CORE_TO_LOGGER_QUEUE); }
    if (mq_resp != (mqd_t)-1) { mq_close(mq_resp); mq_unlink(CORE_TO_UI_QUEUE); }
    ctx->initialized = false;
    printf("[IPC Core] Cleaned up and unlinked message queues.\n");
}

#else

// Fallback implementation for environments without native POSIX mqueue (e.g., MinGW or Windows local test)
bool ipc_core_init(IpcContext *ctx) {
    if (!ctx) return false;
    printf("[IPC Core] Notice: Running in Portable Fallback Mode (POSIX mqueue not native on this OS).\n");
    ctx->initialized = true;
    return true;
}

bool ipc_core_receive_command(IpcContext *ctx, IpcMessage *out_msg) {
    if (!ctx || !out_msg) return false;
    char buffer[256];
    printf("[Simulated IPC] Enter command from UI: ");
    if (!fgets(buffer, sizeof(buffer), stdin)) return false;
    buffer[strcspn(buffer, "\r\n")] = '\0';

    static int seq = 0;
    out_msg->msg_type = MSG_TYPE_CMD;
    out_msg->command_id = ++seq;
    strncpy(out_msg->payload, buffer, sizeof(out_msg->payload) - 1);
    return true;
}

bool ipc_core_send_log(IpcContext *ctx, int cmd_id, const char *log_text, int status_code, double exec_time_us) {
    printf("[IPC -> Logger] [Cmd #%d | Status: %d | Time: %.2f us] %s\n", cmd_id, status_code, exec_time_us, log_text);
    return true;
}

bool ipc_core_send_response(IpcContext *ctx, int cmd_id, const char *resp_text, int status_code, double exec_time_us) {
    printf("[IPC -> UI]     [Cmd #%d | Status: %d | Time: %.2f us] %s\n", cmd_id, status_code, exec_time_us, resp_text);
    return true;
}

void ipc_core_cleanup(IpcContext *ctx) {
    if (ctx) ctx->initialized = false;
}

#endif
