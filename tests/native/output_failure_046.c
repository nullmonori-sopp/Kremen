#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

/* Keep the harness outside the child's file-size limit so the complete JSON
 * diagnostic can still be collected. No permission assumptions are needed. */
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    int errors[2];
    if (pipe(errors)) return 2;
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        close(errors[0]);
        if (dup2(errors[1], STDERR_FILENO) < 0) _exit(2);
        close(errors[1]);
        struct rlimit limit = {1, 1};
        if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
        execvp(argv[1], argv + 1);
        _exit(127);
    }
    close(errors[1]);
    char buffer[4096];
    for (;;) {
        ssize_t count = read(errors[0], buffer, sizeof buffer);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return 2;
        if (!count) break;
        if (fwrite(buffer, 1, (size_t)count, stderr) != (size_t)count) return 2;
    }
    close(errors[0]);
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
