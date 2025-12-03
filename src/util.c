#include "util.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void safe_strcpy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void trim(char *s) {
    if (!s) return;
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

int parse_int(const char *s, int *out) {
    if (!s || !out) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

void time_to_iso8601(time_t t, char *out, size_t size) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, size, "%Y-%m-%dT%H:%M:%S", &tm);
}

void now_iso8601(char *out, size_t size) {
    time_t t = time(NULL);
    time_to_iso8601(t, out, size);
}

int split_kv(char *line, char **key, char **val) {
    char *eq = strchr(line, '=');
    if (!eq) return -1;
    *eq = '\0';
    *key = line;
    *val = eq + 1;
    trim(*key);
    trim(*val);
    return 0;
}

int starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}
