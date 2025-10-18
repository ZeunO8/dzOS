// vmm.c
#include "vmm.h"
#include "device/pic.h"
#include "common/lib.h"
#include "common/printf.h"
#include "cpu/asm.h"

/**
 * From a virtual address, get the index of PTE entry based on the level
 * of it.
 */
#define PTE_INDEX_FROM_VA(va, level) \
    (((((uint64_t)(va)) >> 12) >> ((uint64_t)(level) * 9)) & ((1 << 9) - 1))

/**
 * Number of PTEs in a pagetable
 */
#define PAGETABLE_PTE_COUNT 512

/**
 * Gets the lower boundry of the page which we are trying to access.
 */
#define PAGE_ROUND_DOWN(a) ((a) & ~(PAGE_SIZE - 1))

/**
 * Follow a PTE to the pagetable/frame it is pointing to.
 * Returns the physical address.
 */
static inline uint64_t pte_follow(pte_t pte)
{
	return PTE_ADDR(pte);
}

/**
 * Check if PTE is present
 */
static inline bool pte_is_present(pte_t pte)
{
	return (pte & PTE_P) != 0;
}

/**
 * Check if PTE is a huge page
 */
static inline bool pte_is_huge(pte_t pte)
{
	return (pte & PTE_PS) != 0;
}

/**
 * Check if PTE is user-accessible
 */
static inline bool pte_is_user(pte_t pte)
{
	return (pte & PTE_U) != 0;
}

/**
 * Check if PTE is writable
 */
static inline bool pte_is_writable(pte_t pte)
{
	return (pte & PTE_W) != 0;
}

/**
 * The kernel pagetable which Limine sets up for us. This is in virtual
 * address space.
 */
pagetable_t kernel_pagetable;

/**
 * kernel address which we got from limine
 */
static struct limine_kernel_address_response kernel_address;

/**
 * The address which we start assigning new io memory map requests to.
 * Increments with each IO memmap request.
 *
 * This value looks like the next sector after the code of Limine
 * thus I think it's safe to use it. Limine uses 0xffffffff80000000
 * region.
 */
static uint64_t io_memmap_current_address = 0xfffffffff0000000;

/**
 * Return the address of the PTE in page table pagetable that corresponds to
 * virtual address va. If alloc is true, create any required page-table pages.
 *
 * Intel has two page table types: 5 level and 4 level. For our purpose, we only
 * use 4 level paging. Each page in pagetable contains 512 (4096/8) PTEs.
 * Top 16 bits must be zero.
 * A 64-bit virtual address is split into five fields:
 *   48..63 -- must be zero.
 *   39..47 -- 9 bits of level-4 index.
 *   30..38 -- 9 bits of level-3 index.
 *   21..29 -- 9 bits of level-2 index.
 *   12..20 -- 9 bits of level-1 index.
 *    0..11 -- 12 bits of byte offset within the page.
 */
static pte_t *walk(pagetable_t pagetable, uint64_t va, bool alloc, bool io)
{
	if ((!io && va >= USERSPACE_VA_MAX) || va < USERSPACE_VA_MIN)
		panic("walk");

	for (int level = 3; level > 0; level--)
	{
		pte_t *pte = &pagetable[PTE_INDEX_FROM_VA(va, level)];
		if (pte_is_present(*pte))
		{
			// if PTE is here, just point to it
			pagetable = (pagetable_t)P2V(pte_follow(*pte));
		}
		else
		{
			// PTE does not exist...
			if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
				return 0; // either OOM or N/A page

			memset(pagetable, 0, PAGE_SIZE); // zero the new pagetable

			// Build new PTE with generous permissions (final level controls actual access)
			*pte = PTE_P | PTE_W | PTE_U | PTE_SET_ADDR(V2P(pagetable));
		}
	}

	return &pagetable[PTE_INDEX_FROM_VA(va, 0)];
}

static pte_t *walk_kernel(pagetable_t pagetable, uint64_t va, bool alloc)
{
	if (va < KERNEL_VA_MIN)
		panic("walk_kernel: va below kernel min");

	for (int level = 3; level > 0; level--)
	{
		pte_t *pte = &pagetable[PTE_INDEX_FROM_VA(va, level)];
		if (pte_is_present(*pte))
		{
			pagetable = (pagetable_t)P2V(pte_follow(*pte));
		}
		else
		{
			if (!alloc)
				return NULL;
			pagetable_t new_table = (pagetable_t)kalloc();
			if (!new_table)
				return NULL; // OOM
			memset(new_table, 0, PAGE_SIZE);

			// Build kernel PTE (supervisor-only)
			*pte = PTE_P | PTE_W | PTE_SET_ADDR(V2P(new_table));
			pagetable = new_table;
		}
	}

	return &pagetable[PTE_INDEX_FROM_VA(va, 0)];
}

int vmm_map_kernel_pages(pagetable_t kernel_pagetable, uint64_t va, uint64_t pa,
                         uint64_t size, pte_permissions perm)
{
	if (va % PAGE_SIZE != 0 || pa % PAGE_SIZE != 0 || size % PAGE_SIZE != 0)
		panic("vmm_map_kernel_pages: alignment");

	const uint64_t pages = size / PAGE_SIZE;
	for (uint64_t i = 0; i < pages; i++)
	{
		pte_t *pte = walk_kernel(kernel_pagetable, va + i * PAGE_SIZE, true);
		if (!pte)
			return -1; // OOM

		if (pte_is_present(*pte))
			panic("vmm_map_kernel_pages: remap");

		// Build kernel mapping
		*pte = PTE_P | PTE_SET_ADDR(pa + i * PAGE_SIZE);
		if (perm.writable)
			*pte |= PTE_W;
		if (!perm.executable)
			*pte |= PTE_XD;
		// kernel-only: no PTE_U flag
	}
	return 0;
}

/**
 * Deep copy the pagetable (and not the frames) to another pagetable.
 */
static int copy_pagetable(pagetable_t dst, const pagetable_t src, int level)
{
	// Copy the top-level PTEs (kernel mappings are okay to share)
	memcpy(dst, src, PAGE_SIZE);

	// If this is the leaf level (PT), nothing more to do
	if (level == 0)
		return 0;

	// Iterate over all entries
	for (size_t i = 0; i < PAGETABLE_PTE_COUNT; i++)
	{
		const pte_t pte_src = src[i];

		// Only recurse if entry is present and not a huge page
		if (pte_is_present(pte_src) && !pte_is_huge(pte_src))
		{
			// Allocate a new inner pagetable
			pagetable_t dst_inner = (pagetable_t)kalloc();
			if (!dst_inner)
				return 1; // OOM

			memset(dst_inner, 0, PAGE_SIZE);

			// Get the source inner pagetable
			pagetable_t src_inner = (pagetable_t)P2V(pte_follow(pte_src));

			// Recursively copy inner pagetable
			if (copy_pagetable(dst_inner, src_inner, level - 1) != 0)
			{
				kfree(dst_inner);
				return 1; // propagate OOM
			}

			// Update parent PTE to point to new inner table
			dst[i] = PTE_P | PTE_SET_ADDR(V2P(dst_inner));
			if (pte_is_writable(pte_src))
				dst[i] |= PTE_W;
			if (pte_is_user(pte_src))
				dst[i] |= PTE_U;
			if (pte_src & PTE_XD)
				dst[i] |= PTE_XD;
		}
	}

	return 0;
}

/**
 * We just save the limine_kernel_address_response to be later accessed.
 */
void vmm_init_kernel(const struct limine_kernel_address_response _kernel_address)
{
	kernel_address = _kernel_address;
	kernel_pagetable = (pagetable_t)P2V(get_installed_pagetable());

	// Map essential kernel devices (example: IOAPIC)
	// vmm_map_kernel_pages(
	//     kernel_pagetable,
	//     (uint64_t)P2V(IOAPIC),
	//     IOAPIC,
	//     0x1000,
	//     (pte_permissions){.writable=1, .executable=0, .userspace=0}
	// );
}

void vmm_init_lapic(uint64_t lapic_addr)
{
	vmm_map_kernel_pages(
	    kernel_pagetable,
	    (uint64_t)P2V(lapic_addr),
	    lapic_addr,
	    0x1000,
	(pte_permissions) {
		.writable=1, .executable=0, .userspace=0
	}
	);
}

/**
 * Gets the physical addres of a virtual address from a page table.
 * If user is true, the page must be in user mode. Otherwise it should be
 * in the kernel mode. Returns zero if the page does not exists or the
 * permissions do not match.
 */
uint64_t vmm_walkaddr(pagetable_t pagetable, uint64_t va, bool user)
{
	if (va >= USERSPACE_VA_MAX)
		return 0;

	pte_t *pte = walk(pagetable, va, false, false);
	if (pte == 0)
		return 0;
	if (!pte_is_present(*pte))
		return 0;
	if (pte_is_user(*pte) != user)
		return 0;
	return pte_follow(*pte);
}

/**
 * Create PTEs for virtual addresses starting at va that refer to physical
 * addresses starting at pa. va and size MUST be page-aligned. Returns 0 on
 * success, -1 if walk() couldn't allocate a needed page-table page.
 */
int vmm_map_pages(pagetable_t pagetable, uint64_t va, uint64_t size,
                  uint64_t pa, pte_permissions permissions)
{
	// Sanity checks
	if (pa % PAGE_SIZE != 0)
		panic("vmm_map_pages: pa not aligned");
	if (va % PAGE_SIZE != 0)
		panic("vmm_map_pages: va not aligned");
	if (size % PAGE_SIZE != 0)
		panic("vmm_map_pages: size not aligned");
	if (size == 0)
		panic("vmm_map_pages: size");

	// Map each page individually
	const uint64_t pages_to_map = size / PAGE_SIZE;
	for (uint64_t i = 0; i < pages_to_map; i++)
	{
		const uint64_t current_va = va + i * PAGE_SIZE;
		const uint64_t current_pa = pa + i * PAGE_SIZE;
		pte_t *pte = walk(pagetable, current_va, true, false);
		if (pte == NULL)
			return -1; // OOM
		if (pte_is_present(*pte))
			panic("vmm_map_pages: remap");

		// Build PTE with specified permissions
		*pte = PTE_P | PTE_SET_ADDR(current_pa);
		if (permissions.writable)
			*pte |= PTE_W;
		if (!permissions.executable)
			*pte |= PTE_XD;
		if (permissions.userspace)
			*pte |= PTE_U;
	}
	return 0;
}

/**
 * Allocates pages in a page table. va must be page aligned and the size
 * must be devisable by page size. Returns 0 on success, -1 if walk() couldn't
 * allocate a needed pagetable page.
 *
 * If clear is set, the allocated page will be filled with zero, otherwise 2.
 */
int vmm_allocate(pagetable_t pagetable, uint64_t va, uint64_t size,
                 pte_permissions permissions, bool clear)
{
	// Sanity checks
	if (va % PAGE_SIZE != 0)
		panic("vmm_allocate: va not aligned");
	if (size % PAGE_SIZE != 0)
		panic("vmm_allocate: size not aligned");
	if (size == 0)
		panic("vmm_allocate: size");

	// Allocate pages
	const uint64_t pages_to_alloc = size / PAGE_SIZE;
	for (uint64_t i = 0; i < pages_to_alloc; i++)
	{
		const uint64_t current_va = va + i * PAGE_SIZE;
		void *frame;
		if (clear)
			frame = kcalloc();  // This already zeros
		else
			frame = kalloc();   // This fills with 0x02
		if (frame == NULL)
			return -1;
		pte_t *pte = walk(pagetable, current_va, true, false);
		if (pte == NULL) {
			kfree(frame);
			return -1;
		}
		if (pte_is_present(*pte))
			panic("vmm_allocate: remap");

		// Build PTE
		*pte = PTE_P | PTE_SET_ADDR(V2P(frame));
		if (permissions.writable)
			*pte |= PTE_W;
		if (!permissions.executable)
			*pte |= PTE_XD;
		if (permissions.userspace)
			*pte |= PTE_U;
	}
	return 0;
}

/**
 * Maps a physical address which is used for IO.
 * Returns the virtual address which the region is mapped to.
 */
void *vmm_io_memmap(uint64_t pa, uint64_t size)
{
	// Sanity checks
	if (pa % PAGE_SIZE != 0)
		panic("vmm_io_memmap: pa not aligned");
	if (size % PAGE_SIZE != 0)
		panic("vmm_io_memmap: size not aligned");
	if (size == 0)
		panic("vmm_io_memmap: size");

	// Calculate the virtual address
	uint64_t va =
	    __atomic_fetch_add(&io_memmap_current_address, size, __ATOMIC_RELAXED);

	// Map each page individually
	const uint64_t pages_to_map = size / PAGE_SIZE;
	for (uint64_t i = 0; i < pages_to_map; i++)
	{
		const uint64_t current_va = va + i * PAGE_SIZE;
		const uint64_t current_pa = pa + i * PAGE_SIZE;
		pte_t *pte = walk(kernel_pagetable, current_va, true, true);
		if (pte == NULL)
			return NULL; // OOM
		if (pte_is_present(*pte))
			panic("vmm_io_memmap: remap");

		// Build IO mapping: writable, not executable, not user, cache-disabled
		*pte = PTE_P | PTE_W | PTE_PWT | PTE_PCD | PTE_XD | PTE_SET_ADDR(current_pa);
	}
	return (void *)va;
}

/**
 * Create a pagetable for a program running in userspace.
 * This is done by at first deep copying the kernel pagetable and then mapping
 * the user stuff in the lower addresses. The memory layout is almost as same as
 * https://i.sstatic.net/Ufj7o.png
 *
 * This method does not allocate pages for code, data and heap and only
 * allocates trap pages and the kernel address space. However, a single stack
 * page is allocated.
 */
pagetable_t vmm_user_pagetable_new()
{
	// Allocate a pagetable to be our result
	pagetable_t pagetable = (pagetable_t)kcalloc();
	if (pagetable == NULL)
		return NULL;
	memset(pagetable, 0, PAGE_SIZE);

	// Copy everything to new pagetable (original kernelspace to userspace)
	if (copy_pagetable(pagetable, kernel_pagetable, 3) != 0)
		return NULL;

	// Create dedicated pages
	void *user_stack = NULL, *int_stack = NULL, *syscall_stack = NULL;
	if ((user_stack = kcalloc()) == NULL)
		goto failed;
	if ((int_stack = kcalloc()) == NULL)
		goto failed;
	if ((syscall_stack = kcalloc()) == NULL)
		goto failed;

	// Map pages
	vmm_map_pages(
	    pagetable, USER_STACK_BOTTOM, PAGE_SIZE, V2P(user_stack),
	(pte_permissions) {
		.writable = 1, .executable = 0, .userspace = 1
	});
	vmm_map_pages(
	    pagetable, INTSTACK_VIRTUAL_ADDRESS_BOTTOM, PAGE_SIZE, V2P(int_stack),
	(pte_permissions) {
		.writable = 1, .executable = 0, .userspace = 0
	});
	vmm_map_pages(
	    pagetable, SYSCALLSTACK_VIRTUAL_ADDRESS_BOTTOM, PAGE_SIZE,
	    V2P(syscall_stack),
	(pte_permissions) {
		.writable = 1, .executable = 0, .userspace = 0
	});

	// Done
	return pagetable;

failed:
	if (user_stack != NULL)
		kfree(user_stack);
	if (int_stack != NULL)
		kfree(int_stack);
	if (syscall_stack != NULL)
		kfree(syscall_stack);
	return NULL;
}

/**
 * Recursively deletes the frames of the userspace of a page table.
 * The initial call to this function must be like this:
 * vmm_user_pagetable_free_recursive(pagetable, 0, 3);
 */
static void vmm_user_pagetable_free_recursive(pagetable_t pagetable,
        const uint64_t initial_va,
        int level)
{
	// We shall free the frame at last
	if (level == 0)
	{
		// Note: Pagetable here is not actually a pagetable; Instead,
		// it's the data frame which the virtual address resolves to.
		kfree(pagetable);
		return;
	}

	// Check each entry of the page table
	for (size_t i = 0; i < PAGETABLE_PTE_COUNT; i++)
	{
		const pte_t pte = pagetable[i];
		if (pte_is_present(pte))
		{
			if (pte_is_huge(pte))
				panic("vmm_user_pagetable_free_recursive: huge page");

			// Create the partial va address from steps
			const uint64_t current_va_low = initial_va | (i << (level * 9 + 12));
			const uint64_t current_va_high = initial_va | ((i + 1) << (level * 9 + 12));

			// If the range is outside userspace, skip it (kernel virtual space)
			if ((current_va_high >= USERSPACE_VA_MAX && current_va_low >= USERSPACE_VA_MAX) ||
			        (current_va_high < USERSPACE_VA_MIN && current_va_low < USERSPACE_VA_MIN))
				continue;

			// Descend lower
			const pagetable_t src_inner_pagetable = (pagetable_t)P2V(pte_follow(pte));
			vmm_user_pagetable_free_recursive(src_inner_pagetable, current_va_low, level - 1);
		}
	}

	// Remove the pagetable as well
	kfree(pagetable);
}

/**
 * Frees all pages of a user program from a pagetable.
 * After that, the page table pages are also deleted.
 */
void vmm_user_pagetable_free(pagetable_t pagetable)
{
	// At first free the pages related to user stack, interrupt stack and syscall stack
	uint64_t stack;
	stack = vmm_walkaddr(pagetable, USER_STACK_BOTTOM, true);
	if (stack == 0)
		panic("vmm_user_pagetable_free: user stack");
	kfree((void *)P2V(stack));

	stack = vmm_walkaddr(pagetable, INTSTACK_VIRTUAL_ADDRESS_BOTTOM, false);
	if (stack == 0)
		panic("vmm_user_pagetable_free: interrupt stack");
	kfree((void *)P2V(stack));

	stack = vmm_walkaddr(pagetable, SYSCALLSTACK_VIRTUAL_ADDRESS_BOTTOM, false);
	if (stack == 0)
		panic("vmm_user_pagetable_free: syscall stack");
	kfree((void *)P2V(stack));

	// Now we have to recursively look at any page between USERSPACE_VA_MAX and USERSPACE_VA_MIN
	vmm_user_pagetable_free_recursive(pagetable, 0, 3);
}

/**
 * For an sbrk with positive delta it will allocate the new pages (if needed)
 * and return the new sbrk value to set in the PCB.
 */
uint64_t vmm_user_sbrk_allocate(pagetable_t pagetable, uint64_t old_sbrk,
                                uint64_t delta)
{
	uint64_t new_sbrk = old_sbrk + delta;
	old_sbrk = PAGE_ROUND_UP(old_sbrk);
	for (uint64_t currently_allocating = old_sbrk;
	        currently_allocating < new_sbrk; currently_allocating += PAGE_SIZE)
	{
		if (vmm_allocate(
		            pagetable, currently_allocating, PAGE_SIZE,
		(pte_permissions) {
		.writable = 1, .executable = 0, .userspace = 1
	},
	true) < 0)
		{
			// TODO: handle OOM
			panic("sbrk: OOM");
			break;
		}
	}
	return new_sbrk;
}

/**
 * Deallocated memory in a range of [old_sbrk, old_sbrk - delta].
 * Will not partially remove allocated pages.
 * Will return the new sbrk value set to old_sbrk - delta. This function
 * will not fail unless the page table contains unallocated pages.
 */
uint64_t vmm_user_sbrk_deallocate(pagetable_t pagetable, uint64_t old_sbrk,
                                  uint64_t delta)
{
	uint64_t new_sbrk = old_sbrk - delta;
	old_sbrk = PAGE_ROUND_DOWN(old_sbrk);
	for (uint64_t currently_deallocating = old_sbrk;
	        currently_deallocating > new_sbrk; currently_deallocating -= PAGE_SIZE)
	{
		// Find the frame allocated
		pte_t *pte = walk(pagetable, currently_deallocating, false, false);
		if (pte == NULL)
			panic("vmm_user_sbrk_deallocate: non-existant page");

		// Save physical address before clearing PTE
		uint64_t pa = pte_follow(*pte);

		// Mark it invalid in the page table
		*pte = 0; // Clear entire PTE

		// Invalidate TLB entry if this is the current pagetable
		// Note: You'll need to check if pagetable == current CR3
		vmm_invalidate_page(currently_deallocating);

		// Delete the frame
		kfree((void *)P2V(pa));
	}
	return new_sbrk;
}

/**
 * Copies data to pages of a page table without switching the page table by
 * walking through the given page table.
 *
 * Returns 0 if successful, -1 if failed (invalid virtual address for example)
 */
int vmm_memcpy(pagetable_t pagetable, uint64_t destination_virtual_address,
               const void *source, size_t len, bool userspace)
{
	while (len > 0)
	{
		// Look for the bottom of the page address
		uint64_t va0 = PAGE_ROUND_DOWN(destination_virtual_address);
		if (va0 >= USERSPACE_VA_MAX || va0 < USERSPACE_VA_MIN)
			return -1;

		// Find the page table entry
		pte_t *pte = walk(pagetable, va0, false, false);
		if (pte == 0 || !pte_is_present(*pte) ||
		        pte_is_user(*pte) != userspace || !pte_is_writable(*pte))
			return -1; // invalid page

		uint64_t pa0 = (uint64_t)P2V(pte_follow(*pte));
		uint64_t n = PAGE_SIZE - (destination_virtual_address - va0);
		if (n > len)
			n = len;
		memmove((void *)(pa0 + (destination_virtual_address - va0)), source, n);

		len -= n;
		source += n;
		destination_virtual_address = va0 + PAGE_SIZE;
	}
	return 0;
}

int vmm_zero(pagetable_t pagetable, uint64_t vaddr, uint64_t len) {
	uint8_t zero_buffer[PAGE_SIZE] = {0};
	uint64_t end = vaddr + len;
	while (vaddr < end) {
		uint64_t to_write = (end - vaddr > PAGE_SIZE) ? PAGE_SIZE : (end - vaddr);
		if (vmm_memcpy(pagetable, vaddr, zero_buffer, to_write, true) < 0)
			return -1;
		vaddr += to_write;
	}
	return 0;
}

/**
 * Maps a physical memory region into kernel virtual space for reading/writing,
 * typically used for ACPI tables or device registers.
 * Returns a virtual address pointer.
 */
void *vmm_map_physical(uint64_t phys_start, uint64_t phys_end) {
	if (phys_end <= phys_start)
		panic("vmm_map_physical: invalid range");

	// Align down to page boundary for safety
	uint64_t aligned_start = phys_start & ~(PAGE_SIZE - 1);
	uint64_t offset = phys_start - aligned_start;

	uint64_t size = phys_end - aligned_start;
	if (size % PAGE_SIZE)
		size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	void *va = vmm_io_memmap(aligned_start, size);

	return (void *)((uintptr_t)va + offset);
}

uint64_t vmm_allocate_proc_kernel_stack(uint64_t i)
{
	// Allocate and map kernel stacks for all processes
	uint64_t kernel_va = KERNEL_STACK_BASE + i * KERNEL_STACK_SIZE;
	uint64_t kernel_remaining = KERNEL_STACK_SIZE;

	while (kernel_remaining) {
		void *proc_page = kalloc_for_page_cache();
		if (!proc_page)
			panic("vmm_init_kernel: out of kernel pages for stacks");

		uint64_t kernel_pa = V2P(proc_page);

		// Map one page at a time
		if (vmm_map_kernel_pages(
		            kernel_pagetable,
		            kernel_va + (KERNEL_STACK_SIZE - kernel_remaining),
		            kernel_pa,
		            PAGE_SIZE,
		(pte_permissions) {
		.writable=1, .executable=0, .userspace=0
	}) < 0
	   )
		panic("vmm_init_kernel: failed to map kernel stack page");

		kernel_remaining -= PAGE_SIZE;
	}
	return kernel_va + KERNEL_STACK_SIZE;
}

void vmm_free_proc_kernel_stack(uint64_t i)
{
	uint64_t kernel_va = KERNEL_STACK_BASE + i * KERNEL_STACK_SIZE;
	uint64_t kernel_remaining = KERNEL_STACK_SIZE;

	while (kernel_remaining) {
		uint64_t va = kernel_va + (KERNEL_STACK_SIZE - kernel_remaining);
		pte_t *pte = walk_kernel(kernel_pagetable, va, false);
		if (!pte || !pte_is_present(*pte))
			panic("vmm_free_proc_kernel_stack: missing PTE");

		uint64_t pa = pte_follow(*pte);
		*pte = 0; // clear entire PTE
		vmm_invalidate_page(va); // Invalidate TLB
		kfree((void*)P2V(pa));
		kernel_remaining -= PAGE_SIZE;
	}
}