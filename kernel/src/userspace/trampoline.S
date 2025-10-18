.section .text
.intel_syntax noprefix
.extern process_kstack       # global kernel stack (per CPU if SMP)
.global syscall_handler_asm
.type syscall_handler_asm, @function
syscall_handler_asm:
    # -------------------------------
    # Enter kernel
    # -------------------------------
    swapgs                      # switch GS to kernel
    mov r8, rax                 # syscall number
    mov r9, rsp                 # user RSP

    # -------------------------------
    # Switch to kernel stack
    # -------------------------------
    mov rsp, process_kstack
    # ensure 16-byte alignment for variadic calls
    and rsp, -16

    # -------------------------------
    # Save user context on kernel stack
    # -------------------------------
    push r9                      # user RSP
    push r8                      # syscall number
    push rcx                     # user RIP (from SYSCALL)
    push r11                     # user RFLAGS

    # -------------------------------
    # Call C syscall handler
    # -------------------------------
    mov rdi, r8                  # rdi = syscall number
    mov rsi, rdi                 # optional: more args
    mov rdx, rsi
    mov rcx, rdx
    call syscall_c               # kernel runs on kernel stack

    # -------------------------------
    # Restore user context for return to user
    # -------------------------------
    pop r11                      # restore RFLAGS
    pop rcx                      # restore RIP
    pop r8                       # restore syscall number
    pop rsp                       # restore user RSP to r9

    # -------------------------------
    # Return to user
    # -------------------------------
    swapgs
    sysretq


.section .note.GNU-stack
