// syscall.h
#pragma once

#include "sysnum.h"
#include <stdint.h>
#include <stddef.h>
#include "fs/fs.h"

#define GEN_SYS_0U(RET, NAME, U) RET sys_##NAME()
#define GEN_SYS_1U(RET, NAME, U, ARG1) RET sys_##NAME(ARG1)
#define GEN_SYS_FN1(RET, NAME, U, FN, ARG1) RET sys_##NAME(ARG1)
#define GEN_SYS_RFN2a1b(RET, NAME, U, FN, ARG1, ARG2, ARG3, BLK3) RET sys_##NAME(ARG1, ARG2, ARG3)
#define GEN_SYS_1UV(RET, NAME, U, ARG1) RET sys_##NAME(ARG1)
#define GEN_SYS_2U(RET, NAME, U, ARG1, ARG2) RET sys_##NAME(ARG1, ARG2)
#define GEN_SYS_3U(RET, NAME, U, ARG1, ARG2, ARG3) RET sys_##NAME(ARG1, ARG2, ARG3)
#define GEN_SYS_FN(NAME, U, FN) uint64_t sys_##NAME()
#define GEN_SYS_RFN1(NAME, U, FN, RET, ARG1) RET sys_##NAME(ARG1)
#include <zos/syscall.inc>
#undef GEN_SYS_0U
#undef GEN_SYS_2U
#undef GEN_SYS_1U
#undef GEN_SYS_FN1
#undef GEN_SYS_RFN2a1b
#undef GEN_SYS_1UV
#undef GEN_SYS_3U
#undef GEN_SYS_FN
#undef GEN_SYS_RFN1

char *validate_user_string(const char *user_str, size_t max_len);
bool validate_user_read(const void *ptr, size_t len);
bool validate_user_write(void *ptr, size_t len);