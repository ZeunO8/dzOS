#include "common/lib.h"

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.
void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *pdest = (uint8_t *)dest;
  const uint8_t *psrc = (const uint8_t *)src;

  for (size_t i = 0; i < n; i++) {
    pdest[i] = psrc[i];
  }

  return dest;
}

#define MEMSET_SRC(TYPE) void memset_##TYPE(void* ptr, TYPE val, size_t len) {\
  TYPE* tptr = (TYPE*)ptr;\
  static size_t so_TYPE = sizeof(TYPE);\
  static size_t so_int8 = sizeof(int8_t);\
  static size_t so_int16 = sizeof(int16_t);\
  static size_t so_int32 = sizeof(int32_t);\
  static size_t so_int64 = sizeof(int64_t);\
  size_t size = len / sizeof(TYPE);\
  if (so_TYPE == so_int8 && size > 1 && val == 0)\
    return memset_int64_t(ptr, val, len);\
  size_t i = 0;\
  for (; i < size; ++i)\
    tptr[i] = val;\
  size_t mod = len % sizeof(TYPE);\
  if (!mod)\
    return;\
  size_t ip = i * so_TYPE;\
  size_t lr = len - ip;\
  if (mod < so_int64 && mod >= so_int32)\
    memset_int32_t(ptr + ip, (int32_t)val, lr);\
  else if (mod < so_int32 && mod >= so_int16)\
    memset_int16_t(ptr + ip, (int16_t)val, lr);\
  else\
    memset_int8_t(ptr + ip, (int8_t)val, lr);\
}

MEMSET_SRC(int8_t)
MEMSET_SRC(int16_t)
MEMSET_SRC(int32_t)
MEMSET_SRC(int64_t)

void memset(void* buf, int64_t val, size_t len) {
  return memset_int8_t(buf, val, len);
}

void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *pdest = (uint8_t *)dest;
  const uint8_t *psrc = (const uint8_t *)src;

  if (src > dest) {
    for (size_t i = 0; i < n; i++) {
      pdest[i] = psrc[i];
    }
  } else if (src < dest) {
    for (size_t i = n; i > 0; i--) {
      pdest[i - 1] = psrc[i - 1];
    }
  }

  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;

  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) {
      return p1[i] < p2[i] ? -1 : 1;
    }
  }

  return 0;
}

char *strcpy(char *s, const char *t) {
  char *os = s;
  while ((*s++ = *t++) != 0)
    ;
  return os;
}

int strcmp(const char *p, const char *q) {
  while (*p && *p == *q)
    p++, q++;
  return (uint8_t)*p - (uint8_t)*q;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  if (n == 0)
    return (0);
  do {
    if (*s1 != *s2++)
      return (*(unsigned char *)s1 - *(unsigned char *)--s2);
    if (*s1++ == 0)
      break;
  } while (--n != 0);
  return (0);
}


size_t strlen(const char *s) {
  size_t n;

  for (n = 0; s[n]; n++)
    ;
  return n;
}