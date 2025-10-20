#pragma once
#define GDT_KERNEL_CODE_SEGMENT 0x08
#define GDT_KERNEL_DATA_SEGMENT 0x10
#define GDT_USER_DATA_SEGMENT   0x18
#define GDT_USER_CODE_SEGMENT   0x20
#define GDT_TSS_SEGMENT         0x28
#define IST_DOUBLE_FAULT_STACK_INDEX  1
#define IST_NMI_STACK_INDEX           2
#define IST_MACHINE_CHECK_STACK_INDEX 3
#define IST_DEBUG_STACK_INDEX         4

#ifndef __ASSEMBLER__
#include <stdint.h>
void gdt_init(void);
void tss_init_and_load(void);
void kprint_gdt_init_string(void);
void tss_set_kernel_stack(uint64_t stack_top);
#endif
