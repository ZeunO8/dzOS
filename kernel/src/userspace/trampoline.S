# trampoline.S
.section .text
.intel_syntax noprefix
.extern process_kstack
.global syscall_handler_asm
.type syscall_handler_asm, @function
syscall_handler_asm:
    # ---- Enter kernel and preserve user callee-saved registers ----
    swapgs

    # Save user RSP before switching stacks
    mov r10, rsp                # r10 = user RSP (caller-saved)

    # Switch to kernel stack
    mov rsp, process_kstack
    and rsp, -16                # keep 16-byte alignment

    # Preserve user callee-saved regs as per SysV ABI: r12-r15, rbx, rbp
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    # CRITICAL: Save syscall context BEFORE any function calls
    # Store in callee-saved regs to survive function calls
    mov r15, rax                # save syscall number
    mov r14, rcx                # save user RIP
    mov r13, r11                # save user RFLAGS
    mov r12, rdi                # save arg1
    mov rbp, rsi                # save arg2 (use rbp as GP, already saved)
    mov rbx, rdx                # save arg3 (use rbx, already saved)

    # Save user RSP to another callee-saved reg
    push r10                    # user RSP

    # ---- Save FPU state ----
    # Call C helper - this may clobber caller-saved regs but NOT r12-r15,rbx,rbp
    call fpu_save_current

    # ---- Restore arguments from callee-saved regs ----
    pop r10                     # restore user RSP
    mov rax, r15                # syscall number
    mov rcx, r14                # user RIP (will be in rcx for later)
    mov r11, r13                # user RFLAGS (will be in r11 for later)
    mov rdi, r12                # arg1
    mov rsi, rbp                # arg2
    mov rdx, rbx                # arg3

    # ---- Save minimal return context on kernel stack ----
    push r11                    # user RFLAGS from SYSCALL
    push rcx                    # user RIP from SYSCALL
    push rax                    # syscall number
    push r10                    # saved user RSP

    # ---- Call C handler ----
    # rdi,rsi,rdx already hold a1..a3; set rcx to syscall number (4th arg)
    mov rcx, rax
    call syscall_c

    # ---- Restore for SYSRET ----
    # Restore user FP/SIMD state before returning (preserve rax)
    mov r15, rax                # save return value
    call fpu_load_current
    mov rax, r15                # restore return value

    # Pop minimal return context (preserve rax = syscall return value)
    pop r10                     # user RSP
    add rsp, 8                  # discard saved syscall number
    pop rcx                     # user RIP
    pop r11                     # user RFLAGS

    # Restore preserved callee-saved regs for user context
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    # Set return context
    mov rsp, r10                # user RSP
    # rax = return value from syscall_c
    swapgs
    sysretq

.section .note.GNU-stack