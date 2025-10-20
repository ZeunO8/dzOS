# isr_stubs.S

.intel_syntax noprefix

.section .text
.extern interrupt_dispatch

# Macro to create ISR stub without error code
.macro ISR_NOERRCODE num
.global isr_stub_\num
isr_stub_\num:
    push 0                  # Push dummy error code
    push \num               # Push interrupt number
    jmp isr_common_stub
.endm

# Common ISR handler
isr_common_stub:
    # Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    # Get interrupt number (second push from stub)
    mov rdi, [rsp + 15*8]   # Vector number is 15 qwords up
    
    # Call C handler
    call interrupt_dispatch
    
    # Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    # Remove error code and interrupt number
    add rsp, 16
    
    # Return from interrupt
    iretq

# Create stubs for keyboard (33) and mouse (44)
ISR_NOERRCODE 33    # IRQ1 - Keyboard
ISR_NOERRCODE 44    # IRQ12 - Mouse

.extern sched_timer_handler

.global isr_timer_stub
isr_timer_stub:
    push 0                  # Dummy error code
    push 32                 # Vector number (T_IRQ0)
    
    # Save all GPRs first
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    # CRITICAL: Save user FP/SIMD state BEFORE calling any C code
    # Use per-CPU kernel scratch so we always restore, even if current process changes
    call kernel_fpu_begin
    
    # Call scheduler timer handler with interrupt frame pointer
    mov rdi, rsp
    call sched_timer_handler
    
    # CRITICAL: Restore user FP/SIMD state AFTER C handler returns
    call kernel_fpu_end
    
    # Restore all GPRs
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16
    iretq

.section .note.GNU-stack
