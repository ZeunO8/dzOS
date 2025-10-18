// mem.c
#include "mem.h"
#include "common/lib.h"
#include "common/spinlock.h"
#include "common/printf.h"

/* HHDM offset */
volatile uint64_t hhdm_offset;

/* Free page node used for free list (freed pages land here) */
struct freepage_t { struct freepage_t *next; };

/* Region describing contiguous usable physical memory we haven't carved yet.
 * phys_cur points to the next free physical page inside the region; phys_end is one-past-last.
 */
struct mem_region {
    uintptr_t phys_cur;   /* physical addr of next free page in region (page-aligned) */
    uintptr_t phys_end;   /* physical address one-past-last page (page-aligned) */
    struct mem_region *next;
};

/* Heads */
static struct freepage_t *freepages = NULL;      /* pages returned by kfree() */
static struct mem_region *regions = NULL;        /* lazy regions from init_mem() */

/* Locks */
static struct spinlock freepages_lock;
static struct spinlock regions_lock;

/* Init guard */
static bool mem_initialized = false;

/* Helpers */
static inline void push_free_page(void *page)
{
    struct freepage_t *p = (struct freepage_t *)page;
    p->next = freepages;
    freepages = p;
}

static inline void *pop_free_page(void)
{
    if (!freepages) return NULL;
    void *p = freepages;
    freepages = freepages->next;
    return p;
}

/* Create and append a mem_region node. Caller must hold regions_lock. */
static void append_region(uintptr_t phys_start, uintptr_t phys_end)
{
    if (phys_start >= phys_end) return;
    struct mem_region *r = (struct mem_region *)kalloc_for_page_cache();
    if (!r) {
        static struct mem_region static_pool[16];
        static int static_pool_idx = 0;
        if (static_pool_idx >= (int)(sizeof(static_pool)/sizeof(static_pool[0]))) {
            return;
        }
        r = &static_pool[static_pool_idx++];
    }
    r->phys_cur = phys_start;
    r->phys_end = phys_end;
    r->next = NULL;
    if (!regions) {
        regions = r;
    } else {
        struct mem_region *it = regions;
        while (it->next) it = it->next;
        it->next = r;
    }
}

/* Initialize memory subsystem. Very fast: parse memory map and create regions only. */
void init_mem(uint64_t hhdm_offset_local,
              const struct limine_memmap_response *memory_map)
{
    if (mem_initialized) return;
    if (!memory_map) panic("init_mem: null memory_map");

    hhdm_offset = hhdm_offset_local;

    uint64_t total_pages = 0;

    for (uint64_t i = 0; i < memory_map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memory_map->entries[i];
        if (!entry) continue;
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uintptr_t base = (uintptr_t)entry->base;
        uintptr_t length = (uintptr_t)entry->length;
        if (length < PAGE_SIZE) continue;

        uintptr_t aligned_base = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uintptr_t end = (base + length) & ~(PAGE_SIZE - 1);
        if (aligned_base >= end) continue;

        uint64_t pages_in_region = (end - aligned_base) / PAGE_SIZE;
        total_pages += pages_in_region;

        append_region(aligned_base, end);
    }

    mem_initialized = true;
    ktprintf("Memory initialized lazily: %lu free pages (regions parsed)\n", total_pages);
}

/* kfree: push page to freepages list */
void kfree(void *page)
{
    if (!page) return;
    uintptr_t va = (uintptr_t)page;
    if (va & (PAGE_SIZE - 1)) panic("kfree: unaligned page");
    uint64_t phys = V2P(page);
    if (phys & (PAGE_SIZE - 1)) panic("kfree: V2P produced unaligned phys");

    spinlock_lock(&freepages_lock);
#if defined(DEBUG)
    memset(page, 0xAA, PAGE_SIZE);
#endif
    push_free_page(page);
    spinlock_unlock(&freepages_lock);
}

/* Try to allocate a page from regions (bump allocator). */
static void *alloc_from_regions_for_page_cache(void)
{
    spinlock_lock(&regions_lock);
    struct mem_region *prev = NULL;
    struct mem_region *it = regions;
    while (it) {
        if (it->phys_cur < it->phys_end) {
            uintptr_t phys = it->phys_cur;
            it->phys_cur += PAGE_SIZE;
            if (it->phys_cur >= it->phys_end) {
                if (prev) prev->next = it->next;
                else regions = it->next;
            }
            spinlock_unlock(&regions_lock);
            void *va = P2V(phys);
            return va;
        }
        prev = it;
        it = it->next;
    }
    spinlock_unlock(&regions_lock);
    return NULL;
}

/* kalloc: return a page and memset with pattern 2 */
void *kalloc(void)
{
    spinlock_lock(&freepages_lock);
    void *page = pop_free_page();
    spinlock_unlock(&freepages_lock);
    if (page) {
        memset(page, 2, PAGE_SIZE);
        return page;
    }

    page = alloc_from_regions_for_page_cache();
    if (!page) return NULL;
    memset(page, 2, PAGE_SIZE);
    return page;
}

/* kalloc_for_page_cache: same as kalloc but avoid clearing */
void *kalloc_for_page_cache(void)
{
    spinlock_lock(&freepages_lock);
    void *page = pop_free_page();
    spinlock_unlock(&freepages_lock);
    if (page) return page;

    page = alloc_from_regions_for_page_cache();
    return page;
}

/* kcalloc: allocate and zero a page */
void *kcalloc(void)
{
    spinlock_lock(&freepages_lock);
    void *page = pop_free_page();
    spinlock_unlock(&freepages_lock);
    if (page) {
        memset(page, 0, PAGE_SIZE);
        return page;
    }

    page = alloc_from_regions_for_page_cache();
    if (!page) return NULL;
    memset(page, 0, PAGE_SIZE);
    return page;
}

/* ========== Multi-page allocation support ========== */

/* Allocate multiple contiguous pages from regions.
 * This tries to find a contiguous block in a single region.
 */
static void *alloc_contiguous_from_regions(size_t num_pages)
{
    if (num_pages == 0) return NULL;
    
    size_t bytes_needed = num_pages * PAGE_SIZE;
    
    spinlock_lock(&regions_lock);
    struct mem_region *prev = NULL;
    struct mem_region *it = regions;
    
    while (it) {
        uintptr_t avail = it->phys_end - it->phys_cur;
        
        if (avail >= bytes_needed) {
            // Found a region with enough contiguous space
            uintptr_t phys = it->phys_cur;
            it->phys_cur += bytes_needed;
            
            // Remove region if exhausted
            if (it->phys_cur >= it->phys_end) {
                if (prev) prev->next = it->next;
                else regions = it->next;
            }
            
            spinlock_unlock(&regions_lock);
            void *va = P2V(phys);
            return va;
        }
        
        prev = it;
        it = it->next;
    }
    
    spinlock_unlock(&regions_lock);
    return NULL;
}

/* Try to allocate contiguous pages from the freelist.
 * This is a best-effort approach - we check if we can find
 * num_pages consecutive pages in the freelist.
 */
static void *alloc_contiguous_from_freelist(size_t num_pages)
{
    if (num_pages <= 1) return NULL;
    
    spinlock_lock(&freepages_lock);
    
    // Try to find contiguous pages in freelist
    // This is O(n) and not guaranteed to succeed even if enough pages exist
    struct freepage_t *prev = NULL;
    struct freepage_t *it = freepages;
    
    while (it) {
        uintptr_t base_phys = V2P(it);
        bool found = true;
        struct freepage_t *scan = it;
        struct freepage_t *scan_prev = prev;
        
        // Check if next num_pages-1 pages are contiguous
        for (size_t i = 1; i < num_pages && scan && found; i++) {
            if (!scan->next) {
                found = false;
                break;
            }
            
            uintptr_t expected_phys = base_phys + (i * PAGE_SIZE);
            uintptr_t actual_phys = V2P(scan->next);
            
            if (expected_phys != actual_phys) {
                found = false;
                break;
            }
            
            scan_prev = scan;
            scan = scan->next;
        }
        
        if (found && scan) {
            // Found contiguous block! Remove all pages from freelist
            void *result = it;
            
            if (prev) {
                prev->next = scan->next;
            } else {
                freepages = scan->next;
            }
            
            spinlock_unlock(&freepages_lock);
            return result;
        }
        
        prev = it;
        it = it->next;
    }
    
    spinlock_unlock(&freepages_lock);
    return NULL;
}

/* kalloc_pages: allocate num_pages contiguous pages */
void *kalloc_pages(size_t num_pages)
{
    if (num_pages == 0) return NULL;
    if (num_pages == 1) return kalloc();
    
    // First try to find contiguous pages in freelist (fast path, rarely succeeds)
    void *pages = alloc_contiguous_from_freelist(num_pages);
    if (pages) {
        memset(pages, 2, num_pages * PAGE_SIZE);
        return pages;
    }
    
    // Slow path: allocate from regions (guaranteed contiguous)
    pages = alloc_contiguous_from_regions(num_pages);
    if (pages) {
        memset(pages, 2, num_pages * PAGE_SIZE);
        return pages;
    }
    
    return NULL;
}

/* kfree_pages: free multiple contiguous pages */
void kfree_pages(void *ptr, size_t num_pages)
{
    if (!ptr || num_pages == 0) return;
    
    uintptr_t va = (uintptr_t)ptr;
    if (va & (PAGE_SIZE - 1)) panic("kfree_pages: unaligned pointer");
    
    // Free each page individually to freelist
    // They'll naturally be contiguous in memory
    for (size_t i = 0; i < num_pages; i++) {
        void *page = (void*)(va + i * PAGE_SIZE);
        kfree(page);
    }
}