#include "syscall.h"
#include "cpu/gdt.h"
#include "cpu/asm.h"
#include "cpu/fpu.h"
#include "common/printf.h"
#include <zos/syscall.h>
#include "device/rtc.h"
#include "fs/fs.h"
#include "userspace/proc.h"
#include "userspace/exec.h"

#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_FMASK 0xC0000084

extern void syscall_handler_asm(void);

void init_syscall_table(void)
{
    // Enable syscall
    wrmsr(IA32_EFER, rdmsr(IA32_EFER) | 1);
    // Set CS/SS of kernel/user space
    wrmsr(IA32_STAR, (uint64_t)GDT_KERNEL_CODE_SEGMENT << 32 |
                         ((uint64_t)GDT_USER_DATA_SEGMENT - 8) << 48);
    // Set the address to jump to
    wrmsr(IA32_LSTAR, (uint64_t)syscall_handler_asm);
    // Mask just like Linux kernel:
    // https://elixir.bootlin.com/linux/v6.11.6/source/arch/x86/kernel/cpu/common.c#L2054-L2063
    wrmsr(IA32_FMASK, FLAGS_CF | FLAGS_PF | FLAGS_AF | FLAGS_ZF | FLAGS_SF |
                          FLAGS_TF | FLAGS_IF | FLAGS_DF | FLAGS_OF | FLAGS_IOPL |
                          FLAGS_NT | FLAGS_RF | FLAGS_AC | FLAGS_ID);
    ktprintf("syscall table initialized\n");
}

uint64_t syscall_c(uint64_t a1, uint64_t a2, uint64_t a3,
                    uint64_t syscall_number)
{
    // Preserve user FPU/SSE state across kernel use of XMM/FP
    struct process *p = my_process();
    if (p) {
        fpu_save((void *)p->additional_data.fpu_state);
    }

    uint64_t ret = 0;
    switch (syscall_number)
    {
    // Expand macros into assignments + break to ensure single exit below
    #define GEN_SYS_0U(RET, NAME, U) case SYSCALL_##U: ret = (uint64_t)sys_##NAME(); break
    #define GEN_SYS_1U(RET, NAME, U, ARG1) case SYSCALL_##U: ret = (uint64_t)sys_##NAME((ARG1)a1); break
    #define GEN_SYS_FN1(RET, NAME, U, FN, ARG1) case SYSCALL_##U: FN((ARG1)a1); ret = 0; break
    #define GEN_SYS_RFN2a1b(RET, NAME, U, FN, ARG1, ARG2, ARG3, BLK3) case SYSCALL_##U: ret = (uint64_t)FN((ARG1)a1, (ARG2)a2, BLK3); break
    #define GEN_SYS_1UV(RET, NAME, U, ARG1) case SYSCALL_##U: sys_##NAME((ARG1)a1); ret = 0; break
    #define GEN_SYS_2U(RET, NAME, U, ARG1, ARG2) case SYSCALL_##U: ret = (uint64_t)sys_##NAME((ARG1)a1, (ARG2)a2); break
    #define GEN_SYS_3U(RET, NAME, U, ARG1, ARG2, ARG3) case SYSCALL_##U: ret = (uint64_t)sys_##NAME((ARG1)a1, (ARG2)a2, (ARG3)a3); break
    #define GEN_SYS_FN(NAME, U, FN) case SYSCALL_##U: ret = FN(); break
    #define GEN_SYS_RFN1(NAME, U, FN, RETT, ARG1) case SYSCALL_##U: ret = (uint64_t)FN((ARG1)a1); break
    #include <zos/syscall.inc>
    #undef GEN_SYS_0U
    #undef GEN_SYS_1U
    #undef GEN_SYS_FN1
    #undef GEN_SYS_RFN2a1b
    #undef GEN_SYS_1UV
    #undef GEN_SYS_2U
    #undef GEN_SYS_3U
    #undef GEN_SYS_FN
    #undef GEN_SYS_RFN1
    default:
        ret = 0;
        break;
    }

    // Restore user FPU/SSE state before returning to userspace
    if (p) {
        fpu_load((const void *)p->additional_data.fpu_state);
    }
    return ret;
}
