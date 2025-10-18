#include "fpu.h"

#include <stdint.h>

void fpu_enable(void)
{
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);  // clear EM
    cr0 |=  (1 << 1);  // set MP
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9) | (1 << 10); // OSFXSR | OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");
}

