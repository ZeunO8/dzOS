// mem.h
#pragma once
#ifndef __ASSEMBLER__
#include "limine.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Default page size of Intel CPUs */
#define PAGE_SIZE 4096u

/* Round size up to page boundary */
#define PAGE_ROUND_UP(sz) (((sz) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/* HHDM offset used by Limine. Defined in mem.c */
extern volatile uint64_t hhdm_offset;
extern volatile uint64_t phys_max_end;

/* Convert virtual <-> physical based on current HHDM offset */
#define V2P(ptr) ((uint64_t)((uintptr_t)(ptr) - (uintptr_t)(hhdm_offset)))
#define P2V(ptr) ((void *)((uintptr_t)(ptr) + (uintptr_t)(hhdm_offset)))

static inline bool phys_addr_valid(uint64_t pa)
{
    return pa < phys_max_end && (pa % PAGE_SIZE) == 0;
}

#ifdef __cplusplus
extern "C" {
#endif

void init_mem(uint64_t hhdm_offset,
              const struct limine_memmap_response *memory_map);
void kfree(void *page);
void *kalloc(void);
void *kalloc_for_page_cache(void);
void *kcalloc(void);

/* Multi-page allocation functions */
void *kalloc_pages(size_t num_pages);
void kfree_pages(void *ptr, size_t num_pages);

#ifdef __cplusplus
}
#endif

#endif
