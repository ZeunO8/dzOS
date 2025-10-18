// kmalloc.c - Contiguous allocator with shared-page optimization
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
#include "common/lib.h"
#include <stdbool.h>
#include "mem.h"

#ifndef likely
#define likely(x)   __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
#endif

#define KM_ALIGN                  16u
#define KM_MIN_CLASS              16u
#define KM_MAX_SMALL              (PAGE_SIZE/2)
#define KM_MAGIC_SMALL            0xC0FFEE11u
#define KM_MAGIC_LARGE            0xC0FFEE22u
#define KM_MAGIC_ARENA            0xC0FFEE33u
#define KM_MAGIC_MIXED            0xC0FFEE44u

static inline size_t km_align_up(size_t n, size_t a) {
	return (n+(a-1))&~(a-1);
}
static inline uint32_t km_next_pow2_u32(uint32_t x) {
	x--;
	x|=x>>1;
	x|=x>>2;
	x|=x>>4;
	x|=x>>8;
	x|=x>>16;
	return x+1;
}
static inline uint32_t km_clamp_u32(uint32_t v,uint32_t lo,uint32_t hi) {
	return v<lo?lo:(v>hi?hi:v);
}

/* -------- spinlock -------- */
typedef struct {
	volatile uint32_t v;
} km_spinlock_t;
static inline void km_lock(km_spinlock_t*l) {
	while(__atomic_exchange_n(&l->v,1,__ATOMIC_ACQUIRE)) {
		while(__atomic_load_n(&l->v,__ATOMIC_RELAXED))__builtin_ia32_pause();
	}
}
static inline void km_unlock(km_spinlock_t*l) {
	__atomic_store_n(&l->v,0,__ATOMIC_RELEASE);
}

/* -------- Mixed page for small+medium allocations --------
 * Allows arena (< 16B) and slab (16B-2KB) to coexist in same page
 * Layout: [mixed_page_hdr | allocation_metadata | objects...]
 */
typedef struct {
	uint32_t magic;           // KM_MAGIC_MIXED
	uint32_t used_bytes;      // Total allocated bytes in page
	uint32_t free_offset;     // Next free offset for bump allocation
	uint32_t pad;
} mixed_page_hdr;

/* Per-allocation header stored before each object in mixed page */
typedef struct {
	uint16_t size;            // Actual allocation size
	uint16_t magic_check;     // 0xBEEF for validation
} alloc_hdr;

#define ALLOC_HDR_MAGIC 0xBEEF
#define MIXED_PAGE_META_SIZE sizeof(mixed_page_hdr)
#define MIXED_PAGE_START (MIXED_PAGE_META_SIZE)
#define MIXED_PAGE_MAX_USED (PAGE_SIZE - 256)  // Leave some room for large slabs

/* -------- slab layout -------- */
typedef struct slab_hdr {
	struct slab_hdr* next;
	uint16_t obj_size;
	uint16_t capacity;
	uint16_t free_count;
	uint16_t class_idx;
	uint32_t magic;
	uint16_t top;
} slab_hdr;

typedef struct {
	slab_hdr*  head;
	km_spinlock_t lock;
} km_class_bin;

/* large allocation header */
typedef struct {
	uint32_t magic;
	uint32_t pages;
	uint32_t pad0;
	uint32_t pad1;
} km_large_hdr;

/* Arena for very small allocations */
typedef struct arena_hdr {
	struct arena_hdr* next;
	uint32_t magic;
	uint16_t obj_size;
	uint16_t capacity;
	uint16_t used_count;
	uint16_t pad;
} arena_hdr;

static inline uint8_t* slab_stack_base(slab_hdr*h) {
	return (uint8_t*)(h+1);
}
static inline uint16_t* slab_stack_ptr(slab_hdr*h) {
	return (uint16_t*)slab_stack_base(h);
}
static inline uint8_t* slab_objs_base(slab_hdr*h) {
	return slab_stack_base(h)+h->capacity*sizeof(uint16_t);
}
static inline void* slab_obj_at(slab_hdr*h, uint16_t idx) {
	return slab_objs_base(h)+((size_t)idx*h->obj_size);
}

static inline uint64_t* arena_bitmap(arena_hdr* a) {
	return (uint64_t*)(a+1);
}
static inline uint8_t* arena_objs_base(arena_hdr* a) {
	size_t bitmap_size = ((a->capacity+63)/64) * sizeof(uint64_t);
	return ((uint8_t*)(a+1)) + bitmap_size;
}
static inline void* arena_obj_at(arena_hdr* a, uint16_t idx) {
	return arena_objs_base(a) + ((size_t)idx * KM_ALIGN);
}

/* ----- class table ----- */
static inline uint32_t km_size_to_class_idx(size_t n) {
	uint32_t need=(uint32_t)km_align_up(n,KM_ALIGN);
	need=km_clamp_u32(need,KM_MIN_CLASS,KM_MAX_SMALL);
	uint32_t p2=km_next_pow2_u32(need);
	uint32_t idx=0;
	uint32_t s=p2;
	while((16u<<idx)<s) idx++;
	return idx;
}
static inline uint32_t km_class_idx_to_size(uint32_t idx) {
	return 16u<<idx;
}

#define KM_MAX_CLASS_SIZE (KM_MAX_SMALL)

#if defined(__has_builtin)
#  if __has_builtin(__builtin_ctz)
enum { KM_NUM_BINS = (int)((__builtin_ctz((unsigned)KM_MAX_CLASS_SIZE)
                            - __builtin_ctz((unsigned)KM_MIN_CLASS)) + 1)
                     };
#  else
enum { KM_NUM_BINS = 8 };
#  endif
#elif defined(__GNUC__) || defined(__clang__)
enum { KM_NUM_BINS = (int)((__builtin_ctz((unsigned)KM_MAX_CLASS_SIZE)
                            - __builtin_ctz((unsigned)KM_MIN_CLASS)) + 1)
                     };
#else
enum { KM_NUM_BINS = 8 };
#endif

static km_class_bin km_bins[KM_NUM_BINS];

/* Mixed page allocator for small allocations */
static mixed_page_hdr* mixed_page_head = NULL;
static km_spinlock_t mixed_page_lock;

/* Arena allocator for very small allocations */
static arena_hdr* arena_head = NULL;
static km_spinlock_t arena_lock;

/* ========== Mixed Page Allocator ========== */

static mixed_page_hdr* km_new_mixed_page(void) {
	void* page = kalloc();
	if (!page) return NULL;

	mixed_page_hdr* hdr = (mixed_page_hdr*)page;
	hdr->magic = KM_MAGIC_MIXED;
	hdr->used_bytes = MIXED_PAGE_START;
	hdr->free_offset = MIXED_PAGE_START;
	hdr->pad = 0;

	return hdr;
}

static void* km_alloc_from_mixed_page_locked(size_t size) {
	size_t aligned_size = km_align_up(size, KM_ALIGN);
	size_t total_needed = sizeof(alloc_hdr) + aligned_size;

	mixed_page_hdr* page = mixed_page_head;
	mixed_page_hdr* prev = NULL;

	// Find a page with enough space
	while (page) {
		size_t available = PAGE_SIZE - page->free_offset;
		if (available >= total_needed && page->free_offset < MIXED_PAGE_MAX_USED) {
			// Allocate from this page
			uint8_t* base = (uint8_t*)page;
			alloc_hdr* ahdr = (alloc_hdr*)(base + page->free_offset);
			ahdr->size = (uint16_t)size;
			ahdr->magic_check = ALLOC_HDR_MAGIC;

			void* ptr = (void*)(ahdr + 1);
			page->free_offset += total_needed;
			page->used_bytes += total_needed;

			return ptr;
		}
		prev = page;
		page = (mixed_page_hdr*)((uint8_t*)page + PAGE_SIZE); // Move to next page

		// Check if we've walked off the list
		if ((uintptr_t)page & (PAGE_SIZE - 1)) break;
	}

	// Need a new page
	mixed_page_hdr* new_page = km_new_mixed_page();
	if (!new_page) return NULL;

	// Link it at the head
	if (mixed_page_head) {
		// Store link in unused space (we'll use a simple linked list later)
		// For now, just replace head
	}
	mixed_page_head = new_page;

	// Allocate from new page
	uint8_t* base = (uint8_t*)new_page;
	alloc_hdr* ahdr = (alloc_hdr*)(base + new_page->free_offset);
	ahdr->size = (uint16_t)size;
	ahdr->magic_check = ALLOC_HDR_MAGIC;

	void* ptr = (void*)(ahdr + 1);
	new_page->free_offset += total_needed;
	new_page->used_bytes += total_needed;

	return ptr;
}

static void km_free_from_mixed_page_locked(void* p) {
	// Get allocation header
	alloc_hdr* ahdr = (alloc_hdr*)p - 1;
	if (ahdr->magic_check != ALLOC_HDR_MAGIC) {
		return; // Invalid free
	}

	// Get page header using proper mask
	uintptr_t addr = (uintptr_t)p;
	uintptr_t page_addr = addr & ~((uintptr_t)(PAGE_SIZE - 1));
	mixed_page_hdr* page = (mixed_page_hdr*)page_addr;

	// Verify it's actually a mixed page
	if (page->magic != KM_MAGIC_MIXED) {
		return; // Invalid page
	}

	// Mark as freed by zeroing magic
	ahdr->magic_check = 0;

	size_t freed_size = sizeof(alloc_hdr) + km_align_up(ahdr->size, KM_ALIGN);

	if (page->used_bytes >= freed_size) {
		page->used_bytes -= freed_size;
	}

	// If page is completely empty, could free it
	// For now, keep it around for reuse
}

/* ========== Arena Allocator ========== */

static arena_hdr* km_new_arena(void) {
	void* page = kalloc();
	if (!page) return NULL;

	arena_hdr* a = (arena_hdr*)page;
	a->magic = KM_MAGIC_ARENA;
	a->obj_size = KM_ALIGN;
	a->next = NULL;

	size_t meta = sizeof(arena_hdr);
	size_t avail = PAGE_SIZE - meta;
	uint32_t cap = (uint32_t)((avail * 8) / (1 + 16*8));
	size_t bitmap_size = ((cap + 63) / 64) * sizeof(uint64_t);
	size_t obj_area = avail - bitmap_size;
	cap = (uint32_t)(obj_area / KM_ALIGN);

	a->capacity = (uint16_t)cap;
	a->used_count = 0;
	a->pad = 0;

	uint64_t* bm = arena_bitmap(a);
	size_t bm_size = ((cap + 63) / 64);
	for (size_t i = 0; i < bm_size; i++) {
		bm[i] = 0;
	}

	return a;
}

static void* km_alloc_from_arena_locked(void) {
	arena_hdr* a = arena_head;

	while (a && a->used_count >= a->capacity) {
		a = a->next;
	}

	if (!a) {
		a = km_new_arena();
		if (!a) return NULL;
		a->next = arena_head;
		arena_head = a;
	}

	uint64_t* bm = arena_bitmap(a);
	uint32_t bm_size = (a->capacity + 63) / 64;

	for (uint32_t i = 0; i < bm_size; i++) {
		if (bm[i] != ~0ULL) {
			for (int bit = 0; bit < 64; bit++) {
				uint32_t idx = i * 64 + bit;
				if (idx >= a->capacity) break;

				if (!(bm[i] & (1ULL << bit))) {
					bm[i] |= (1ULL << bit);
					a->used_count++;
					return arena_obj_at(a, idx);
				}
			}
		}
	}

	return NULL;
}

static void km_free_to_arena_locked(arena_hdr* a, void* ptr) {
	uintptr_t off = (uintptr_t)((uint8_t*)ptr - arena_objs_base(a));
	uint16_t idx = (uint16_t)(off / KM_ALIGN);

	if (idx >= a->capacity) return;

	uint64_t* bm = arena_bitmap(a);
	uint32_t word_idx = idx / 64;
	uint32_t bit_idx = idx % 64;

	bm[word_idx] &= ~(1ULL << bit_idx);
	a->used_count--;
}

static inline arena_hdr* km_owner_arena(void* p) {
	return (arena_hdr*)((uintptr_t)p & ~(uintptr_t)(PAGE_SIZE-1));
}

/* ========== Slab Allocator ========== */

static slab_hdr* km_new_slab(uint32_t class_idx) {
	void* page=kalloc();
	if(!page) return NULL;
	uint32_t obj_size=km_class_idx_to_size(class_idx);
	uint8_t* p=(uint8_t*)page;
	slab_hdr* h=(slab_hdr*)p;
	*h=(slab_hdr) {
		.next=NULL,.obj_size=(uint16_t)obj_size,.capacity=0,.free_count=0,.class_idx=(uint16_t)class_idx,.magic=KM_MAGIC_SMALL,.top=0
	};
	size_t meta=sizeof(slab_hdr);
	size_t avail=PAGE_SIZE-meta;
	uint32_t cap=(uint32_t)(avail/(obj_size+sizeof(uint16_t)));
	size_t stack_bytes=cap*sizeof(uint16_t);
	size_t obj_area=PAGE_SIZE-(meta+stack_bytes);
	cap=(uint32_t)(obj_area/obj_size);
	h->capacity=(uint16_t)cap;
	h->free_count=(uint16_t)cap;
	h->top=(uint16_t)cap;
	uint16_t* stk=slab_stack_ptr(h);
	for(uint16_t i=0; i<cap; i++) stk[i]=cap-1-i;
	return h;
}

static inline void* km_alloc_from_bin_locked(km_class_bin*bin,uint32_t class_idx) {
	slab_hdr* h=bin->head;
	if(!h) {
		h=km_new_slab(class_idx);
		if(!h) return NULL;
		h->next=NULL;
		bin->head=h;
	}
	if(h->top==0) {
		slab_hdr* n=h->next;
		if(!n) {
			n=km_new_slab(class_idx);
			if(!n) return NULL;
			n->next=NULL;
			bin->head=n;
			h=n;
		}
		else {
			bin->head=n;
			h=n;
		}
	}
	uint16_t* stk=slab_stack_ptr(h);
	uint16_t idx=stk[--h->top];
	h->free_count--;
	if(h->top==0) {
		slab_hdr* n=h->next;
		bin->head=(n?n:h);
		if(n) h->next=NULL;
	}
	return slab_obj_at(h,idx);
}

static inline uint16_t slab_index_of(slab_hdr*h, void* ptr) {
	uintptr_t off=(uintptr_t)((uint8_t*)ptr - slab_objs_base(h));
	return (uint16_t)(off / h->obj_size);
}

static inline void km_free_to_bin_locked(km_class_bin*bin,slab_hdr*h,void*ptr) {
	uint16_t idx=slab_index_of(h,ptr);
	uint16_t* stk=slab_stack_ptr(h);
	stk[h->top++]=idx;
	h->free_count++;
	if(h->free_count==1) {
		h->next=bin->head;
		bin->head=h;
	}
	if(h->free_count==h->capacity) {
		if(bin->head==h) bin->head=h->next;
		kfree((void*)h);
	}
}

static inline slab_hdr* km_owner_slab(void* p) {
	return (slab_hdr*)((uintptr_t)p & ~(uintptr_t)(PAGE_SIZE-1));
}

/* ========== Public API ========== */

void kmalloc_init(void) {
	for(uint32_t i=0; i<KM_NUM_BINS; i++) {
		km_bins[i].head=NULL;
		km_bins[i].lock.v=0;
	}
	arena_head = NULL;
	arena_lock.v = 0;
	mixed_page_head = NULL;
	mixed_page_lock.v = 0;
}

void* kmalloc(size_t n) {
	if(unlikely(n==0)) n=1;

	// Use mixed page for small-medium allocations (< 2KB)
	if (n <= KM_MAX_SMALL) {
		km_lock(&mixed_page_lock);
		void* obj = km_alloc_from_mixed_page_locked(n);
		km_unlock(&mixed_page_lock);
		return obj;
	} else {
		// Large allocation: allocate contiguous pages
		size_t need=km_align_up(n,KM_ALIGN);
		size_t total=need+sizeof(km_large_hdr);
		size_t pages=PAGE_ROUND_UP(total)/PAGE_SIZE;

		void* base = kalloc_pages(pages);
		if(!base) return NULL;

		km_large_hdr* h=(km_large_hdr*)base;
		h->magic=KM_MAGIC_LARGE;
		h->pages=(uint32_t)pages;
		h->pad0=0;
		h->pad1=0;

		return (uint8_t*)base+sizeof(km_large_hdr);
	}
}

void* kcmalloc(size_t n) {
	void* ptr = kmalloc(n);
	if (!ptr) return NULL;
	memset(ptr, 0, n);
	return ptr;
}

void kmfree(void* p) {
	if(!p) return;

	uintptr_t base=((uintptr_t)p)&~(uintptr_t)(PAGE_SIZE-1);
	uint32_t* magic_ptr = (uint32_t*)base;
	uint32_t magic = *magic_ptr;

	if (magic == KM_MAGIC_MIXED) {
		// Mixed page allocation
		km_lock(&mixed_page_lock);
		km_free_from_mixed_page_locked(p);
		km_unlock(&mixed_page_lock);
	} else if (magic == KM_MAGIC_ARENA) {
		// Arena allocation
		arena_hdr* owner = km_owner_arena(p);
		km_lock(&arena_lock);
		km_free_to_arena_locked(owner, p);
		km_unlock(&arena_lock);
	} else if (magic == KM_MAGIC_SMALL) {
		// Slab allocation
		slab_hdr* owner = km_owner_slab(p);
		uint32_t idx = owner->class_idx;
		km_class_bin* bin = &km_bins[idx];
		km_lock(&bin->lock);
		km_free_to_bin_locked(bin, owner, p);
		km_unlock(&bin->lock);
	} else if (magic == KM_MAGIC_LARGE) {
		// Large allocation
		km_large_hdr* lh = (km_large_hdr*)((uintptr_t)p - sizeof(km_large_hdr));
		if (lh->magic == KM_MAGIC_LARGE) {
			void* base = (void*)lh;
			uint32_t pages = lh->pages;
			kfree_pages(base, pages);
		} else {
			kfree((void*)base);
		}
	} else {
		// Unknown allocation
		kfree((void*)base);
	}
}