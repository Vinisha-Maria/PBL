#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_core.h"

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <mqueue.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("====================================================\n");
    printf("        MOCK UI PROCESS (FOR INTEGRATION TEST)      \n");
    printf("====================================================\n");

    mqd_t mq_ui = mq_open(UI_TO_CORE_QUEUE, O_WRONLY);
    if (mq_ui == (mqd_t)-1) {
        perror("Error opening UI_TO_CORE_QUEUE (Ensure Core is running first)");
        return 1;
    }

    mqd_t mq_resp = mq_open(CORE_TO_UI_QUEUE, O_RDONLY);

    char buffer[256];
    int cmd_id = 0;

    printf("Connected to Core. Type instructions (e.g. 'LOAD R0 10', 'EXIT'):\n");
    while (1) {
        printf("UI> ");
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        IpcMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_type = (strcmp(buffer, "EXIT") == 0) ? MSG_TYPE_SHUTDOWN : MSG_TYPE_CMD;
        msg.command_id = ++cmd_id;
        strncpy(msg.payload, buffer, sizeof(msg.payload) - 1);

        if (mq_send(mq_ui, (const char *)&msg, sizeof(msg), 0) < 0) {
            perror("mq_send failed");
            break;
        }

        if (msg.msg_type == MSG_TYPE_SHUTDOWN) break;

        // Read response from Core
        if (mq_resp != (mqd_t)-1) {
            IpcMessage resp;
            if (mq_receive(mq_resp, (char *)&resp, sizeof(resp), NULL) >= 0) {
                printf("[Response from Core] Status: %d | Time: %.2f us | Result: %s\n",
                       resp.status_code, resp.execution_time_us, resp.payload);
            }
        }
    }

    mq_close(mq_ui);
    if (mq_resp != (mqd_t)-1) mq_close(mq_resp);
    return 0;
}
#else
int main(void) {
    printf("Mock UI POSIX Message Queue runner requires a POSIX/Linux environment.\n");
    return 0;
}
#endif
