#ifndef KREMEN_BOOTSTRAP_RUNTIME_H
#define KREMEN_BOOTSTRAP_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

void rt_init(int argc, char **argv);
void rt_cleanup(void);
int64_t rt_argc(void);
int64_t rt_len(const char *value);
int64_t rt_at(const char *value, int64_t index);
const char *rt_slice(const char *value, int64_t start, int64_t end);
const char *rt_cat(const char *left, const char *right);
bool rt_equal(const char *left, const char *right);
const char *rt_number(int64_t value);
const char *rt_arg(int64_t index);
const char *rt_read(const char *path);
int64_t rt_write(const char *path, const char *value);
int64_t rt_error(const char *path, int64_t line, int64_t column, const char *message);
int64_t rt_iadd(int64_t left, int64_t right, const char *path, int64_t line, int64_t column);
int64_t rt_isub(int64_t left, int64_t right, const char *path, int64_t line, int64_t column);
int64_t rt_imul(int64_t left, int64_t right, const char *path, int64_t line, int64_t column);
int64_t rt_idiv(int64_t left, int64_t right, const char *path, int64_t line, int64_t column);
int64_t rt_imod(int64_t left, int64_t right, const char *path, int64_t line, int64_t column);
int64_t rt_ineg(int64_t value, const char *path, int64_t line, int64_t column);

#endif
