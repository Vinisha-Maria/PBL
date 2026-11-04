/* test_sock.c - unit test for ipc_sock (server/client, HELLO, PING/PONG, timeout, close, responder) */
#define _GNU_SOURCE
#include "ipc_sock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(name, cond) do { int ok_ = (cond); printf("[%s] %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) fails++; } while (0)

static void on_shutdown(void) { _exit(0); }

int main(void)
{
    int srv = sock_ctrl_server_start();
    CHECK("server start", srv >= 0);

    pid_t pid = fork();
    if (pid == 0) {                                          /* ---- client ---- */
        int fd = sock_ctrl_client_connect("core");
        if (fd < 0) _exit(10);
        sock_ctrl_send(fd, "READY");
        sock_ctrl_start_responder(fd, on_shutdown);          /* answers PING, exits on SHUTDOWN */
        sleep(10);
        _exit(11);
    }

    char name[64], msg[CTRL_MSG_MAX];
    int fd = sock_ctrl_accept(srv, name, sizeof name, 3000);
    CHECK("accept + HELLO", fd >= 0 && strcmp(name, "core") == 0);
    CHECK("recv READY", sock_ctrl_recv(fd, msg, sizeof msg, 3000) > 0 && strcmp(msg, "READY") == 0);

    CHECK("PING sent", sock_ctrl_send(fd, "PING") == 0);
    CHECK("PONG received", sock_ctrl_recv(fd, msg, sizeof msg, 3000) > 0 && strcmp(msg, "PONG") == 0);
    CHECK("recv timeout returns 0", sock_ctrl_recv(fd, msg, sizeof msg, 200) == 0);
    CHECK("message too long rejected", ({ char big[300]; memset(big, 'x', 299); big[299] = 0; sock_ctrl_send(fd, big) == -1; }));

    sock_ctrl_send(fd, "SHUTDOWN");
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK("client exits on SHUTDOWN", WIFEXITED(st) && WEXITSTATUS(st) == 0);
    CHECK("recv after peer closed returns -1", sock_ctrl_recv(fd, msg, sizeof msg, 500) == -1);

    sock_ctrl_close(fd);
    sock_ctrl_close(srv);
    sock_ctrl_unlink();
    CHECK("client_connect fails when no server", ({ int c = sock_ctrl_client_connect("x"); c == -1; }));

    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
