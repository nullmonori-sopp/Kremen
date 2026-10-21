#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double seconds(struct timeval value) {
    return (double)value.tv_sec + (double)value.tv_usec / 1000000.0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        (void)fputs("usage: kremen-bench-time RUNS COMMAND [ARG ...]\n", stderr);
        return 2;
    }
    char *end;
    errno = 0;
    uintmax_t runs = strtoumax(argv[1], &end, 10);
    if (errno || *argv[1] == '-' || !*argv[1] || *end || runs == 0 || runs > 100000) {
        (void)fputs("benchmark: RUNS must be between 1 and 100000\n", stderr);
        return 2;
    }
    struct timespec start, finish;
    struct rusage cpu_start, cpu_finish;
    if (clock_gettime(CLOCK_MONOTONIC, &start) || getrusage(RUSAGE_CHILDREN, &cpu_start)) {
        perror("benchmark: start timer");
        return 1;
    }
    for (uintmax_t iteration = 0; iteration < runs; ++iteration) {
        pid_t child = fork();
        if (child < 0) {
            perror("benchmark: fork");
            return 1;
        }
        if (child == 0) {
            execvp(argv[2], argv + 2);
            perror("benchmark: exec");
            _exit(127);
        }
        int status;
        pid_t waited;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            perror("benchmark: waitpid");
            return 1;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            (void)fprintf(stderr, "benchmark: child failed during run %" PRIuMAX " (exit %d)\n", iteration + 1, code);
            return code;
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &finish) || getrusage(RUSAGE_CHILDREN, &cpu_finish)) {
        perror("benchmark: finish timer");
        return 1;
    }
    double elapsed = (double)(finish.tv_sec - start.tv_sec)
        + (double)(finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    (void)fprintf(stderr, "real %.9f\nuser %.9f\nsys %.9f\nruns %" PRIuMAX "\nmean %.9f\n", elapsed,
        seconds(cpu_finish.ru_utime) - seconds(cpu_start.ru_utime),
        seconds(cpu_finish.ru_stime) - seconds(cpu_start.ru_stime), runs, elapsed / (double)runs);
    return 0;
}
