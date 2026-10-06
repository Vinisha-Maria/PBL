#ifndef IPC_CORE_H
#define IPC_CORE_H

#include <stdbool.h>
#include <stddef.h>

#define UI_TO_CORE_QUEUE    "/pbl_ui_to_core"
#define CORE_TO_LOGGER_QUEUE "/pbl_core_to_logger"
#define CORE_TO_UI_QUEUE    "/pbl_core_to_ui"

#define MAX_MSG_PAYLOAD 256

typedef enum {
    MSG_TYPE_CMD = 1,       // Command from UI
    MSG_TYPE_RESP,          // Response back to UI
    MSG_TYPE_LOG,           // Log event to Logger
    MSG_TYPE_SHUTDOWN       // Graceful shutdown signal
} IpcMessageType;

typedef struct {
    int msg_type;
    int command_id;
    char payload[MAX_MSG_PAYLOAD];
    int status_code;
    double execution_time_us;
} IpcMessage;

typedef struct {
    int mq_in_desc;    // UI -> Core incoming queue descriptor
    int mq_log_desc;   // Core -> Logger outgoing queue descriptor
    int mq_resp_desc;  // Core -> UI response queue descriptor
    bool initialized;
} IpcContext;

// Functions
bool ipc_core_init(IpcContext *ctx);
bool ipc_core_receive_command(IpcContext *ctx, IpcMessage *out_msg);
bool ipc_core_send_log(IpcContext *ctx, int cmd_id, const char *log_text, int status_code, double exec_time_us);
bool ipc_core_send_response(IpcContext *ctx, int cmd_id, const char *resp_text, int status_code, double exec_time_us);
void ipc_core_cleanup(IpcContext *ctx);

#endif // IPC_CORE_H
