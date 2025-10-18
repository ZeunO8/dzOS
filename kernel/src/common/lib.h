#pragma once
#include <stddef.h>
#include <stdint.h>

// Max/Min from https://stackoverflow.com/a/3437484/4213397
#define MAX_SAFE(a, b)                                                         \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a > _b ? _a : _b;                                                         \
  })

#define MIN_SAFE(a, b)                                                         \
  ({                                                                           \
    __typeof__(a) _a = (a);                                                    \
    __typeof__(b) _b = (b);                                                    \
    _a < _b ? _a : _b;                                                         \
  })

#define MEMSET_HEADER(TYPE) void memset_##TYPE(void* buf, TYPE val, size_t len)

void *memcpy(void *dest, const void *src, size_t n);

MEMSET_HEADER(int8_t);
MEMSET_HEADER(int16_t);
MEMSET_HEADER(int32_t);
MEMSET_HEADER(int64_t);

void memset(void* buf, int64_t val, size_t len);

void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
char *strcpy(char *s, const char *t);
int strcmp(const char *p, const char *q);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);