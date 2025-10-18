# fpu.S
# Fixed for Clang assembler on x86_64 (AT&T syntax by default)

.section .text
.global fpu_save
.type fpu_save, @function
fpu_save:
    fxsave64 (%rdi)
    ret

.global fpu_load
.type fpu_load, @function
fpu_load:
    fxrstor64 (%rdi)
    ret

# --- Arithmetic Functions ---
.global __adddf3
.type __adddf3, @function
__adddf3:
    addsd %xmm1, %xmm0
    ret

.global __subdf3
.type __subdf3, @function
__subdf3:
    subsd %xmm1, %xmm0
    ret

.global __muldf3
.type __muldf3, @function
__muldf3:
    mulsd %xmm1, %xmm0
    ret

.global __divdf3
.type __divdf3, @function
__divdf3:
    divsd %xmm1, %xmm0
    ret

# --- Conversion Functions ---
.global __fixdfsi
.type __fixdfsi, @function
__fixdfsi:
    cvttsd2si %xmm0, %rax
    ret

.global __floatsidf
.type __floatsidf, @function
__floatsidf:
    cvtsi2sd %edi, %xmm0
    ret

# fpu.S patched __fixunsdfdi for Clang x86_64 AT&T syntax
.global __fixunsdfdi
.type __fixunsdfdi, @function
__fixunsdfdi:
    ucomisd .L_2_pow_63(%rip), %xmm0
    jbe .L_fixunsdfdi_small
    subsd .L_2_pow_63(%rip), %xmm0
    cvttsd2si %xmm0, %rax
    bts $63, %rax          # Set the high bit instead of OR with 64-bit immediate
    ret
.L_fixunsdfdi_small:
    cvttsd2si %xmm0, %rax
    ret

.global __floatundidf
.type __floatundidf, @function
__floatundidf:
    test %rdi, %rdi
    jns .L_floatundidf_small
    mov %rdi, %rax
    and $1, %rdi
    andq $-2, %rax
    push %rdi
    mov %rax, %rdi
    call .L_floatundidf_positive
    pop %rdi
    cvtsi2sd %rdi, %xmm1
    addsd %xmm1, %xmm0
    ret
.L_floatundidf_small:
.L_floatundidf_positive:
    cvtsi2sdq %rdi, %xmm0
    ret

# --- Comparison Functions ---
.global __gtdf2
.type __gtdf2, @function
__gtdf2:
    xor %eax, %eax
    ucomisd %xmm1, %xmm0
    ja .L_gtdf2_greater
    jb .L_gtdf2_less
    ret
.L_gtdf2_greater:
    mov $1, %eax
    ret
.L_gtdf2_less:
    mov $-1, %eax
    ret

.global __ltdf2
.type __ltdf2, @function
__ltdf2:
    xor %eax, %eax
    ucomisd %xmm1, %xmm0
    ja .L_ltdf2_greater
    jb .L_ltdf2_less
    ret
.L_ltdf2_greater:
    mov $1, %eax
    ret
.L_ltdf2_less:
    mov $-1, %eax
    ret

.global __ledf2
.type __ledf2, @function
__ledf2:
    xor %eax, %eax
    ucomisd %xmm1, %xmm0
    ja .L_ledf2_greater
    ret
.L_ledf2_greater:
    mov $1, %eax
    ret

.global __gedf2
.type __gedf2, @function
__gedf2:
    xor %eax, %eax
    ucomisd %xmm1, %xmm0
    jb .L_gedf2_less
    ret
.L_gedf2_less:
    mov $1, %eax
    ret

.section .rodata
.align 8
.L_2_pow_63:
    .quad 0x43e0000000000000

.section .note.GNU-stack,"",@progbits
