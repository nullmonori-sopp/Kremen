#ifndef KREMEN_SELFHOST_HOST_H
#define KREMEN_SELFHOST_HOST_H
#include <stdbool.h>
#include <stdint.h>
int64_t host_argc(void);
const char *host_env(const char *name);
int64_t host_setenv(const char *name, const char *value);
const char *host_executable(void);
const char *host_resource(const char *name);
const char *host_tempdir(void);
const char *host_stage_dir(const char *output);
int64_t host_remove(const char *path);
int64_t host_rmdir(const char *path);
bool host_same_file(const char *left, const char *right);
int64_t host_publish(const char *temporary, const char *destination);
int64_t host_exec_begin(const char *program);
int64_t host_exec_arg(const char *argument);
int64_t host_exec_run(void);
int64_t host_print(const char *text, bool to_stderr);
bool host_equal_files(const char *left, const char *right);
#endif
