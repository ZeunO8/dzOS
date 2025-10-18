.intel_syntax noprefix
.section .text
.global invoke_syscall
.type invoke_syscall, @function

invoke_syscall:
    /* Arguments: 
       rdi = syscall number
       rsi = arg1
       rdx = arg2
       rcx = arg3 (optional, can extend to r10, r8, r9 for full 6-arg convention)
    */
    mov rax, rdi      # syscall number
    mov rdi, rsi      # first argument
    mov rsi, rdx      # second argument
    mov rdx, rcx      # third argument
    syscall
    ret

.section .note.GNU-stack