#include "include/sysnum.h"
#include <stddef.h>
#include <stdint.h>
#include "userspace/proc.h"
#include "userspace/exec.h"
#include "device/rtc.h"

uint64_t invoke_syscall(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#define GEN_SYS_0U(RET, NAME, U) \
    RET NAME(void) { \
        return (RET)invoke_syscall(SYSCALL_##U, 0, 0, 0); \
    }

#define GEN_SYS_1U(RET, NAME, U, ARG1) \
    RET NAME(ARG1 a1) { \
        return (RET)invoke_syscall(SYSCALL_##U, (uint64_t)a1, 0, 0); \
    }

#define GEN_SYS_2U(RET, NAME, U, ARG1, ARG2) \
    RET NAME(ARG1 a1, ARG2 a2) { \
        return (RET)invoke_syscall(SYSCALL_##U, (uint64_t)a1, (uint64_t)a2, 0); \
    }

#define GEN_SYS_3U(RET, NAME, U, ARG1, ARG2, ARG3) \
    RET NAME(ARG1 a1, ARG2 a2, ARG3 a3) { \
        return (RET)invoke_syscall(SYSCALL_##U, (uint64_t)a1, (uint64_t)a2, (uint64_t)a3); \
    }

#define GEN_SYS_FN1(RET, NAME, U, FN, ARG1) \
    RET NAME(ARG1 a1) { \
        FN((ARG1)a1); \
    }

#define GEN_SYS_RFN1(NAME, U, FN, RET, ARG1) \
    RET NAME(ARG1 a1) { \
        return FN((ARG1)a1); \
    }

#define GEN_SYS_RFN2a1b(RET, NAME, U, FN, ARG1, ARG2, ARG3, BLK3) \
    RET NAME(ARG1 a1, ARG2 a2, ARG3 a3) { \
        return FN((ARG1)a1, (ARG2)a2, (ARG3)a3); \
    }

#define GEN_SYS_1UV(RET, NAME, U, ARG1) \
    RET NAME(ARG1 a1) { \
        return (RET)invoke_syscall(SYSCALL_##U, (uint64_t)a1, 0, 0); \
    }

#define GEN_SYS_FN(NAME, U, FN) \
    uint64_t NAME(void) { \
        return FN(); \
    }

#include "include/syscall.inc"

#undef GEN_SYS_0U
#undef GEN_SYS_1U
#undef GEN_SYS_2U
#undef GEN_SYS_3U
#undef GEN_SYS_FN1
#undef GEN_SYS_RFN1
#undef GEN_SYS_RFN2a1b
#undef GEN_SYS_1UV
#undef GEN_SYS_FN