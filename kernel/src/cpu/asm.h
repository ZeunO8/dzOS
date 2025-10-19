#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLAGS_CF (1UL << 0)
#define FLAGS_PF (1UL << 2)
#define FLAGS_AF (1UL << 4)
#define FLAGS_ZF (1UL << 6)
#define FLAGS_SF (1UL << 7)
#define FLAGS_TF (1UL << 8)
#define FLAGS_IF (1UL << 9)
#define FLAGS_DF (1UL << 10)
#define FLAGS_OF (1UL << 11)
#define FLAGS_IOPL (3UL << 12)
#define FLAGS_NT (1UL << 14)
#define FLAGS_RF (1UL << 16)
#define FLAGS_VM (1UL << 17)
#define FLAGS_AC (1UL << 18)
#define FLAGS_VIF (1UL << 19)
#define FLAGS_VIP (1UL << 20)
#define FLAGS_ID (1UL << 21)

#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void wait_for_interrupt(void) { __asm__ volatile("hlt"); }

__attribute__((noreturn)) static inline void halt(void)
{
  for (;;)
    __asm__ volatile("hlt");
}

static inline void install_pagetable(uint64_t pagetable_address)
{
  __asm__ volatile("mov %0, %%cr3" : : "r"(pagetable_address & 0xFFFFFFFFFFFFF000ULL));
}

static inline uint64_t get_installed_pagetable()
{
  uint64_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3 & 0xFFFFFFFFFFFFF000ULL;
}

static inline uint64_t rdmsr(uint32_t msr)
{
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
  uint32_t lo = (uint32_t)value;
  uint32_t hi = (uint32_t)(value >> 32);
  __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static inline uint64_t read_rflags(void)
{
  uint64_t flags;
  __asm__ volatile("pushfq; pop %0" : "=r"(flags));
  return flags;
}

static inline void cli(void) { __asm__ volatile("cli"); }

static inline void sti(void) { __asm__ volatile("sti"); }

static inline uint64_t get_tsc(void)
{
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static inline bool is_interrupts_enabled(void)
{
  return (read_rflags() & FLAGS_IF) != 0;
}
