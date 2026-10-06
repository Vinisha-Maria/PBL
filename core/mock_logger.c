#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ipc_core.h"

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <mqueue.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("====================================================\n");
    printf("      MOCK LOGGER PROCESS (FOR INTEGRATION TEST)    \n");
    printf("====================================================\n");

    mqd_t mq_log = mq_open(CORE_TO_LOGGER_QUEUE, O_RDONLY);
    if (mq_log == (mqd_t)-1) {
        perror("Error opening CORE_TO_LOGGER_QUEUE (Ensure Core is running first)");
        return 1;
    }

    FILE *log_file = fopen("simulator.log", "a");
    if (!log_file) {
        perror("Error opening simulator.log file");
    }

    printf("Logger listening on %s...\n", CORE_TO_LOGGER_QUEUE);

    IpcMessage msg;
    while (1) {
        ssize_t bytes = mq_receive(mq_log, (char *)&msg, sizeof(msg), NULL);
        if (bytes < 0) {
            perror("mq_receive failed");
            break;
        }

        time_t now = time(NULL);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        printf("[%s] [Cmd #%d] [Status: %d] [Latency: %.2f us] %s\n",
               time_str, msg.command_id, msg.status_code, msg.execution_time_us, msg.payload);

        if (log_file) {
            fprintf(log_file, "[%s] [Cmd #%d] [Status: %d] [Latency: %.2f us] %s\n",
                    time_str, msg.command_id, msg.status_code, msg.execution_time_us, msg.payload);
            fflush(log_file);
        }

        if (msg.status_code == 0 && strcmp(msg.payload, "EXIT") == 0) break;
    }

    if (log_file) fclose(log_file);
    mq_close(mq_log);
    return 0;
}
#else
int main(void) {
    printf("Mock Logger POSIX Message Queue runner requires a POSIX/Linux environment.\n");
    return 0;
}
#endif
