#pragma once
#include <stddef.h>
#include <stdint.h>

#define GEN_SYS_0U(RET, NAME, U) RET NAME()
#define GEN_SYS_1U(RET, NAME, U, ARG1) RET NAME(ARG1)
#define GEN_SYS_FN1(RET, NAME, U, FN, ARG1) RET NAME(ARG1)
#define GEN_SYS_RFN2a1b(RET, NAME, U, FN, ARG1, ARG2, ARG3, BLK3) RET NAME(ARG1, ARG2, ARG3)
#define GEN_SYS_1UV(RET, NAME, U, ARG1) RET NAME(ARG1)
#define GEN_SYS_2U(RET, NAME, U, ARG1, ARG2) RET NAME(ARG1, ARG2)
#define GEN_SYS_3U(RET, NAME, U, ARG1, ARG2, ARG3) RET NAME(ARG1, ARG2, ARG3)
#define GEN_SYS_FN(NAME, U, FN) uint64_t NAME()
#define GEN_SYS_RFN1(NAME, U, FN, RET, ARG1) RET NAME(ARG1)
#include "include/syscall.inc"
#undef GEN_SYS_0U
#undef GEN_SYS_2U
#undef GEN_SYS_1U
#undef GEN_SYS_FN1
#undef GEN_SYS_RFN2a1b
#undef GEN_SYS_1UV
#undef GEN_SYS_3U
#undef GEN_SYS_FN
#undef GEN_SYS_RFN1

// Yield the program and give the time slice to another program
static inline void yield(void) { __asm__ volatile("int 0x80"); }