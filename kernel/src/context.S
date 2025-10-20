# context.S

#include "cpu/gdt.h"

.section .text
.intel_syntax noprefix
.global reload_segments
.type reload_segments, @function
# Load the GDT and reload the segments to point to kernel data and user segment
# Function prototype is void reload_segments(void *gdt)
reload_segments:
    lgdt [rdi]
    push GDT_KERNEL_CODE_SEGMENT
    lea rax, [.reload_CS]
    push rax
    retfq
.reload_CS:
    # SS must contain the data segment
    mov ax, GDT_KERNEL_DATA_SEGMENT
    mov ss, ax
    # Other segments contain zero
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret

.extern process_kstack # global kernel stack
.global context_switch_to_user
.type context_switch_to_user, @function
context_switch_to_user:
    # Save kernel context
    mov QWORD PTR [rsi + 0x00], r15
    mov QWORD PTR [rsi + 0x08], r14
    mov QWORD PTR [rsi + 0x10], r13
    mov QWORD PTR [rsi + 0x18], r12
    mov QWORD PTR [rsi + 0x20], rbp
    mov QWORD PTR [rsi + 0x28], rbx
    mov QWORD PTR [rsi + 0x30], r11
    mov QWORD PTR [rsi + 0x38], r10
    mov QWORD PTR [rsi + 0x40], r9
    mov QWORD PTR [rsi + 0x48], r8
    mov QWORD PTR [rsi + 0x50], rsi
    mov QWORD PTR [rsi + 0x58], rdi
    mov QWORD PTR [rsi + 0x60], rdx
    mov QWORD PTR [rsi + 0x68], rcx
    mov QWORD PTR [rsi + 0x70], rax
    mov QWORD PTR [rsi + 0x78], rsp

    mov process_kstack, rsp
    
    # Load sysretq parameters FIRST (while still on kernel stack)
    mov rcx, QWORD PTR [rdi + 0x80]    # RIP -> RCX
    mov r11, QWORD PTR [rdi + 0x88]    # RFLAGS -> R11
    
    # Load user registers (use rdi as base since we don't need it anymore)
    mov r15, QWORD PTR [rdi + 0x00]
    mov r14, QWORD PTR [rdi + 0x08]
    mov r13, QWORD PTR [rdi + 0x10]
    mov r12, QWORD PTR [rdi + 0x18]
    mov rbp, QWORD PTR [rdi + 0x20]
    mov rbx, QWORD PTR [rdi + 0x28]
    # r11 already loaded (sysretq RFLAGS)
    mov r10, QWORD PTR [rdi + 0x38]
    mov r9,  QWORD PTR [rdi + 0x40]
    mov r8,  QWORD PTR [rdi + 0x48]
    mov rsi, QWORD PTR [rdi + 0x50]
    # rdi loaded last
    mov rdx, QWORD PTR [rdi + 0x60]
    # rcx already loaded (sysretq RIP)
    mov rax, QWORD PTR [rdi + 0x70]
    mov rsp, QWORD PTR [rdi + 0x78]    # Switch to user stack
    mov rdi, QWORD PTR [rdi + 0x58]    # Load rdi last since we used it as base

    swapgs
    sysretq


.global context_switch_to_kernel
.type context_switch_to_kernel, @function
context_switch_to_kernel:
    test rdx, rdx
    jz .save_from_regs

    # Save user context from interrupt frame pointed by rdx
    mov rax, QWORD PTR [rdx + 0x00]
    mov QWORD PTR [rsi + 0x00], rax      # r15
    mov rax, QWORD PTR [rdx + 0x08]
    mov QWORD PTR [rsi + 0x08], rax      # r14
    mov rax, QWORD PTR [rdx + 0x10]
    mov QWORD PTR [rsi + 0x10], rax      # r13
    mov rax, QWORD PTR [rdx + 0x18]
    mov QWORD PTR [rsi + 0x18], rax      # r12
    mov rax, QWORD PTR [rdx + 0x20]
    mov QWORD PTR [rsi + 0x30], rax      # r11
    mov rax, QWORD PTR [rdx + 0x28]
    mov QWORD PTR [rsi + 0x38], rax      # r10
    mov rax, QWORD PTR [rdx + 0x30]
    mov QWORD PTR [rsi + 0x40], rax      # r9
    mov rax, QWORD PTR [rdx + 0x38]
    mov QWORD PTR [rsi + 0x48], rax      # r8
    mov rax, QWORD PTR [rdx + 0x40]
    mov QWORD PTR [rsi + 0x20], rax      # rbp
    mov rax, QWORD PTR [rdx + 0x68]
    mov QWORD PTR [rsi + 0x28], rax      # rbx
    mov rax, QWORD PTR [rdx + 0x50]
    mov QWORD PTR [rsi + 0x50], rax      # rsi
    mov rax, QWORD PTR [rdx + 0x48]
    mov QWORD PTR [rsi + 0x58], rax      # rdi
    mov rax, QWORD PTR [rdx + 0x58]
    mov QWORD PTR [rsi + 0x60], rax      # rdx
    mov rax, QWORD PTR [rdx + 0x60]
    mov QWORD PTR [rsi + 0x68], rax      # rcx
    mov rax, QWORD PTR [rdx + 0x70]
    mov QWORD PTR [rsi + 0x70], rax      # rax
    mov rax, QWORD PTR [rdx + 0xA0]
    mov QWORD PTR [rsi + 0x78], rax      # rsp
    mov rax, QWORD PTR [rdx + 0x88]
    mov QWORD PTR [rsi + 0x80], rax      # rip
    mov rax, QWORD PTR [rdx + 0x98]
    mov QWORD PTR [rsi + 0x88], rax      # rflags
    jmp .restore_kernel

.save_from_regs:
    mov QWORD PTR [rsi + 0x00], r15
    mov QWORD PTR [rsi + 0x08], r14
    mov QWORD PTR [rsi + 0x10], r13
    mov QWORD PTR [rsi + 0x18], r12
    mov QWORD PTR [rsi + 0x20], rbp
    mov QWORD PTR [rsi + 0x28], rbx
    mov QWORD PTR [rsi + 0x30], r11
    mov QWORD PTR [rsi + 0x38], r10
    mov QWORD PTR [rsi + 0x40], r9
    mov QWORD PTR [rsi + 0x48], r8
    mov QWORD PTR [rsi + 0x50], rsi
    mov QWORD PTR [rsi + 0x58], rdi
    mov QWORD PTR [rsi + 0x60], rdx
    mov QWORD PTR [rsi + 0x68], rcx
    mov QWORD PTR [rsi + 0x70], rax
    mov QWORD PTR [rsi + 0x78], rsp      # save user stack
.restore_kernel:

    mov r13, rdi                        # r13 = pointer to kernel context struct

    mov r15, QWORD PTR [r13 + 0x00]
    mov r14, QWORD PTR [r13 + 0x08]
    mov r12, QWORD PTR [r13 + 0x18]
    mov rbp, QWORD PTR [r13 + 0x20]
    mov rbx, QWORD PTR [r13 + 0x28]
    mov r11, QWORD PTR [r13 + 0x30]
    mov r10, QWORD PTR [r13 + 0x38]
    mov r9,  QWORD PTR [r13 + 0x40]
    mov r8,  QWORD PTR [r13 + 0x48]
    mov rsi, QWORD PTR [r13 + 0x50]
    mov rdi, QWORD PTR [r13 + 0x58]
    mov rdx, QWORD PTR [r13 + 0x60]
    mov rcx, QWORD PTR [r13 + 0x68]
    mov rax, QWORD PTR [r13 + 0x70]

    # Load kernel stack pointer
    mov rsp, QWORD PTR [r13 + 0x78]
    
    ret

.section .note.GNU-stack