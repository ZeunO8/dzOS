// vmm.h
#pragma once
#ifndef __ASSEMBLER__
#include "limine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif
#include "mem.h"

/**
 * Max virtual address of a memory in a process. This is not The max actual
 * virtual address because Limine puts its stuff in there. So we go a little bit
 * lower because we need to mutally map trampoline to kernel and userspace.
 */
#define USERSPACE_VA_MAX (1ULL << 46)
/**
 * Min virutal address of a memory in a process (4MB)
 */
#define USERSPACE_VA_MIN (1ULL << 22)

/**
 * Where we should put the top of the stack in the virtual address space
 */
#define USER_STACK_TOP USERSPACE_VA_MAX

/**
 * Where we should put the bottom of the stack in the virtual address space
 */
#define USER_STACK_SIZE 0x8000
#define USER_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_SIZE)

/**
 * Interrupt stack virtual address. Used when userspace is switching to kernel
 * space to store the interrupt stack. Interrupt stack is one page only.
 */
#define INTSTACK_SIZE 0x8000
#define INTSTACK_VIRTUAL_ADDRESS_TOP USER_STACK_BOTTOM
#define INTSTACK_VIRTUAL_ADDRESS_BOTTOM                                        \
  (INTSTACK_VIRTUAL_ADDRESS_TOP - INTSTACK_SIZE)

/**
 * The stack which we can use for syscall of user programs. The first values
 * (top 16 bytes) is the RSP and then RAX of the user program. Note that
 * the RAX value will get overwritten in the syscall subroutine.
 * View trampoline.S for more info.
 * (This points to top of stack)
 */
#define SYSCALLSTACK_SIZE 0x8000
#define SYSCALLSTACK_VIRTUAL_ADDRESS_TOP INTSTACK_VIRTUAL_ADDRESS_BOTTOM
#define SYSCALLSTACK_VIRTUAL_ADDRESS_BOTTOM                                    \
  (SYSCALLSTACK_VIRTUAL_ADDRESS_TOP - SYSCALLSTACK_SIZE)

#define KERNEL_STACK_SIZE 0x4000
#define KERNEL_STACK_GUARD_SIZE PAGE_SIZE
#define KERNEL_STACK_TOTAL_SIZE (KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_SIZE)
#define KERNEL_VA_MIN (1ULL << 47)
#define KERNEL_STACK_BASE 0xFFFF900000000000ULL


#ifndef __ASSEMBLER__
/* Compile-time invariants for stack layout and sizes */
_Static_assert((USER_STACK_SIZE % PAGE_SIZE) == 0, "USER_STACK_SIZE must be page aligned");
_Static_assert((INTSTACK_SIZE % PAGE_SIZE) == 0, "INTSTACK_SIZE must be page aligned");
_Static_assert((SYSCALLSTACK_SIZE % PAGE_SIZE) == 0, "SYSCALLSTACK_SIZE must be page aligned");

_Static_assert(USER_STACK_TOP == USERSPACE_VA_MAX, "USER_STACK_TOP must equal USERSPACE_VA_MAX");
_Static_assert(INTSTACK_VIRTUAL_ADDRESS_TOP == USER_STACK_BOTTOM, "INTSTACK top must equal USER stack bottom");
_Static_assert(SYSCALLSTACK_VIRTUAL_ADDRESS_TOP == INTSTACK_VIRTUAL_ADDRESS_BOTTOM, "SYSCALL stack top must equal INT stack bottom");
_Static_assert(SYSCALLSTACK_VIRTUAL_ADDRESS_BOTTOM >= USERSPACE_VA_MIN, "Stacks must remain within userspace VA window");

/* PTE bit definitions (Intel x86-64 format) */
#define PTE_P       (1ULL << 0)   // Present
#define PTE_W       (1ULL << 1)   // Writable
#define PTE_U       (1ULL << 2)   // User-accessible
#define PTE_PWT     (1ULL << 3)   // Write-through
#define PTE_PCD     (1ULL << 4)   // Cache disable
#define PTE_A       (1ULL << 5)   // Accessed
#define PTE_D       (1ULL << 6)   // Dirty
#define PTE_PS      (1ULL << 7)   // Page size (huge page)
#define PTE_G       (1ULL << 8)   // Global
#define PTE_XD      (1ULL << 63)  // Execute disable

/* Physical address mask (bits 12-45 for standard 4-level paging) */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL
#define PTE_ADDR_SHIFT 12

/* Helper macros for PTE manipulation */
#define PTE_ADDR(pte)           ((pte) & PTE_ADDR_MASK)
#define PTE_FLAGS(pte)          ((pte) & ~PTE_ADDR_MASK)
#define PTE_SET_ADDR(addr)      (((uint64_t)(addr)) & PTE_ADDR_MASK)

/**
 * Some set of PTE permissions
 */
typedef struct {
  // If 1, we can write to this page
  uint8_t writable : 1;
  // If 1, we can execute this page
  uint8_t executable : 1;
  // If 1, this is a userspace page
  uint8_t userspace : 1;
} pte_permissions;

/**
 * Page table entry - now just a uint64_t instead of bitfield struct
 */
typedef uint64_t pte_t;

/**
 * Each pagetable contains multiple (or 512 to be exact) PTEs
 */
typedef pte_t *pagetable_t;

extern pagetable_t kernel_pagetable;

void vmm_init_kernel(const struct limine_kernel_address_response);
void vmm_init_lapic(uint64_t lapic_addr);
uint64_t vmm_walkaddr(pagetable_t pagetable, uint64_t va, bool user);
int vmm_map_pages(pagetable_t pagetable, uint64_t va, uint64_t size,
                  uint64_t pa, pte_permissions permissions);
int vmm_map_kernel_pages(
  pagetable_t kernel_pagetable,
  uint64_t va,
  uint64_t pa,
  uint64_t size,
  pte_permissions perm);
int vmm_allocate(pagetable_t pagetable, uint64_t va, uint64_t size,
                 pte_permissions permissions, bool clear);
void *vmm_io_memmap(uint64_t pa, uint64_t size);
pagetable_t vmm_user_pagetable_new();
void vmm_user_pagetable_free(pagetable_t pagetable);
uint64_t vmm_user_sbrk_allocate(pagetable_t pagetable, uint64_t old_sbrk,
                                uint64_t delta);
uint64_t vmm_user_sbrk_deallocate(pagetable_t pagetable, uint64_t old_sbrk,
                                  uint64_t delta);
int vmm_memcpy(pagetable_t pagetable, uint64_t destination_virtual_address,
               const void *source, size_t len, bool userspace);
int vmm_zero(pagetable_t pagetable, uint64_t vaddr, uint64_t len);
void *vmm_map_physical(uint64_t phys_start, uint64_t phys_end);

uint64_t vmm_allocate_proc_kernel_stack(uint64_t i);
void vmm_free_proc_kernel_stack(uint64_t i);

/**
 * Validate that a user pointer points to valid, mapped, user-accessible memory.
 * @param pagetable The page table to check against
 * @param ptr User pointer to validate
 * @param len Length of the memory region
 * @param writable If true, also check that memory is writable
 * @return true if valid, false otherwise
 */
bool vmm_validate_user_ptr(pagetable_t pagetable, const void *ptr, size_t len, bool writable);

/**
 * Validate and copy a null-terminated string from user space.
 * @param pagetable The page table to check against
 * @param user_str User pointer to string
 * @param kernel_buf Kernel buffer to copy into
 * @param max_len Maximum length to copy (including null terminator)
 * @return Number of bytes copied (excluding null), or -1 on error
 */
int vmm_copy_user_string(pagetable_t pagetable, const char *user_str, char *kernel_buf, size_t max_len);

static inline void vmm_invalidate_page(uint64_t va)
{
    __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
}

static inline void vmm_flush_tlb(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3)::"memory");
    __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}
#endif
