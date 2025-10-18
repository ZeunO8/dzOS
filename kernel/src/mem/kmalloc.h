// kmalloc.h
#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * Initialize the kernel memory allocator.
 * Must be called once during kernel initialization, after init_mem().
 */
void kmalloc_init(void);

/**
 * Allocate n bytes of kernel memory.
 * 
 * Memory allocation strategy:
 * - Sizes < 16 bytes: Contiguous arena allocator (bitmap-based, ~240 objects/page)
 * - Sizes 16-2048 bytes: Slab allocator (power-of-2 size classes)
 * - Sizes > 2048 bytes: Multi-page allocator (contiguous pages via kalloc_pages)
 * 
 * @param n Number of bytes to allocate (minimum 1)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* kmalloc(size_t n);

/**
 * Allocate n bytes of kernel memory and zero it.
 * Same allocation strategy as kmalloc(), but clears the memory before returning.
 * 
 * @param n Number of bytes to allocate (minimum 1)
 * @return Pointer to zeroed memory, or NULL on failure
 */
void* kcmalloc(size_t n);

/**
 * Free memory previously allocated by kmalloc() or kcmalloc().
 * Safe to call with NULL pointer (no-op).
 * 
 * Automatically detects allocation type (arena/slab/large) via magic numbers.
 * 
 * @param p Pointer to memory to free, or NULL
 */
void kmfree(void* p);