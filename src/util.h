#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <time.h>

void safe_strcpy(char *dst, size_t dst_size, const char *src);
void trim(char *s);
int parse_int(const char *s, int *out);
void time_to_iso8601(time_t t, char *out, size_t size);
void now_iso8601(char *out, size_t size);
int split_kv(char *line, char **key, char **val);
int starts_with(const char *s, const char *prefix);

#endif
