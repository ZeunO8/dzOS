# trampoline.S
.section .text
.intel_syntax noprefix
.extern process_kstack
.global syscall_handler_asm
.type syscall_handler_asm, @function
syscall_handler_asm:
    # ---- Enter kernel ----
    swapgs

    # Save user context that SYSCALL clobbers or we need
    # rax = syscall number
    # rcx = user RIP (from SYSCALL)
    # r11 = user RFLAGS (from SYSCALL)
    mov r15, rax                # save syscall number
    mov r14, rcx                # save user RIP
    mov r13, r11                # save user RFLAGS
    mov r12, rsp                # save user RSP

    # ---- Switch to kernel stack ----
    mov rsp, process_kstack
    and rsp, -16                # keep 16-byte alignment

    # (Optional) save for debugging
    push r12                    # user RSP
    push r15                    # syscall number
    push r14                    # user RIP
    push r13                    # user RFLAGS

    # ---- Call C handler ----
    # On SYSCALL entry:
    #   rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5, r9=a6 (Linux-ABI style)
    # For a normal C call (SysV):
    #   rdi, rsi, rdx, rcx, r8, r9
    # We only use a1..a3 plus syscall_number as the 4th arg.
    # rdi,rsi,rdx already good; set rcx to the syscall number.
    mov rcx, r15

    call syscall_c

    # ---- Restore for SYSRET ----
    pop r13                     # user RFLAGS
    pop r14                     # user RIP
    pop r15                     # syscall number (unused on return)
    pop r12                     # user RSP

    mov r11, r13                # RFLAGS -> r11
    mov rcx, r14                # RIP    -> rcx
    mov rsp, r12                # user RSP

    # rax = return value from syscall_c
    swapgs
    sysretq

.section .note.GNU-stack
