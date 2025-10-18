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
    # Save all general-purpose registers into the "from" context (kernel context)
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
    mov QWORD PTR [rsi + 0x78], rsp # save kernel stack

    # Set the global kernel stack pointer for syscall/trap handling
    mov process_kstack, rsp

    mov rbx, QWORD PTR [rsi + 0x58] # rbx := to_context (base pointer)

    # save the saved-rbx field on the stack so we don't clobber our base pointer
    push QWORD PTR [rbx + 0x28]        # push(saved_rbx)

    # Load destination/user registers from *rbx* (to_context)
    mov r15, QWORD PTR [rbx + 0x00]
    mov r14, QWORD PTR [rbx + 0x08]
    mov r13, QWORD PTR [rbx + 0x10]
    mov r12, QWORD PTR [rbx + 0x18]
    mov rbp, QWORD PTR [rbx + 0x20]

    mov r11, QWORD PTR [rbx + 0x30]
    mov r10, QWORD PTR [rbx + 0x38]
    mov r9,  QWORD PTR [rbx + 0x40]
    mov r8,  QWORD PTR [rbx + 0x48]
    mov rsi, QWORD PTR [rbx + 0x50]
    mov rdi, QWORD PTR [rbx + 0x58]
    mov rdx, QWORD PTR [rbx + 0x60]
    mov rcx, QWORD PTR [rbx + 0x68]
    mov rax, QWORD PTR [rbx + 0x70]

    mov rsp, QWORD PTR [rbx + 0x78]
    mov rcx, QWORD PTR [rbx + 0x80]    # RIP -> RCX for sysretq
    mov r11, QWORD PTR [rbx + 0x88]   # RFLAGS -> R11 for sysretq

    pop rbx                            # restore the saved rbx into rbx


    swapgs
    sysretq

.global context_switch_to_kernel
.type context_switch_to_kernel, @function
context_switch_to_kernel:
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