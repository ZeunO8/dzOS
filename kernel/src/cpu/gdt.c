#include "cpu/gdt.h"
#include "mem/mem.h"
#include "mem/vmm.h"
#include "common/printf.h"
#include <stdint.h>

/**
 * The structure of GDT register
 */
struct gdtr
{
  uint16_t limit;
  uint64_t ptr;
} __attribute__((packed));

/**
 * Each GDT entry is like this
 */
struct gdt_desc
{
  uint16_t limit;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_hi;
} __attribute__((packed));

/**
 * Or each GDT entry can continue if it is a System Segment Descriptor.
 * Read more:
 * https://wiki.osdev.org/Global_Descriptor_Table#Long_Mode_System_Segment_Descriptor
 */
struct gdt_sys_desc_upper
{
  uint32_t base_very_high;
  uint32_t reserved;
} __attribute__((packed));

/**
 * Each GDT entry can either be a normal entry or the continuation of upper
 * part of a system segment descriptor.
 */
union gdt_entry
{
  struct gdt_desc normal;
  struct gdt_sys_desc_upper sys_desc_upper;
};

/**
 * The GDT entries needed for our OS
 */
static union gdt_entry gdt_entries[] = {
    // First segment is NULL
    {.normal = {0}},
    // 64-Bit Code Segement (Kernel)
    {.normal = {.limit = 0x0000,
                .base_low = 0x0000,
                .base_mid = 0x00,
                .access = 0b10011010,
                .granularity = 0b00100000,
                .base_hi = 0x00}},
    // 64-Bit Data Segement (Kernel)
    {.normal = {.limit = 0x0000,
                .base_low = 0x0000,
                .base_mid = 0x00,
                .access = 0b10010010,
                .granularity = 0b00000000,
                .base_hi = 0x00}},
    // 64-Bit Data Segement (User)
    {.normal = {.limit = 0x0000,
                .base_low = 0x0000,
                .base_mid = 0x00,
                .access = 0b11110010,
                .granularity = 0b00000000,
                .base_hi = 0x00}},
    // 64-Bit Code Segement (User)
    {.normal = {.limit = 0x0000,
                .base_low = 0x0000,
                .base_mid = 0x00,
                .access = 0b11111010,
                .granularity = 0b00100000,
                .base_hi = 0x00}},
    // TSS. Limit and base will be filled before this gets loaded
    {.normal = {.limit = 0x0000,
                .base_low = 0x0000,
                .base_mid = 0x00,
                .access = 0b10001001,
                .granularity = 0b00000000,
                .base_hi = 0x00}},
    // TSS continued...
    {.sys_desc_upper = {
         .base_very_high = 0,
         .reserved = 0,
     }}};

/**
 * The TSS entry
 */
struct tss_entry {
    uint32_t reserved1;
    
    // Stack pointers for privilege levels 0-2
    uint64_t sp0;  // Kernel stack (CPL 0)
    uint64_t sp1;  // Ring 1 (unused in x86-64)
    uint64_t sp2;  // Ring 2 (unused in x86-64)
    
    uint64_t reserved2;
    
    // Interrupt Stack Table (IST)
    // Used for critical interrupts that need separate stacks
    uint64_t ist[7];
    
    uint32_t reserved3;
    uint32_t reserved4;
    uint16_t reserved5;
    uint16_t io_bitmap_base;
} __attribute__((packed));

/**
 * The TSS which will be loaded. At first, we initialize everything
 * with zero and then fill them in tss_init.
 */
static struct tss_entry tss = {0};

extern void reload_segments(void *gdt); // defined in snippet.S

/**
 * Setup the Task State Segment for this (and only) core and
 * put the address of it in GDT.
 *
 * This function must be called before gdt_init.
 */
void tss_init_and_load(void)
{
  ktprintf("[TSS] Initializing Task State Segment\n");
  
  // Allocate stacks for IST entries
  // IST[0] = Double Fault (already done in original code)
  tss.ist[IST_DOUBLE_FAULT_STACK_INDEX - 1] = (uint64_t)kalloc() + PAGE_SIZE;
  
  // IST[1] = NMI (Non-Maskable Interrupt)
  tss.ist[IST_NMI_STACK_INDEX - 1] = (uint64_t)kalloc() + PAGE_SIZE;
  
  // IST[2] = Machine Check Exception
  tss.ist[IST_MACHINE_CHECK_STACK_INDEX - 1] = (uint64_t)kalloc() + PAGE_SIZE;
  
  // IST[3] = Debug/Breakpoint
  tss.ist[IST_DEBUG_STACK_INDEX - 1] = (uint64_t)kalloc() + PAGE_SIZE;
  
  // SP0 will be updated per-process by scheduler
  // This is the default kernel interrupt stack
  tss.sp0 = INTSTACK_VIRTUAL_ADDRESS_TOP;
  
  // SP1 and SP2 unused in long mode
  tss.sp1 = 0;
  tss.sp2 = 0;
  
  // IO bitmap at end of TSS (none used)
  tss.io_bitmap_base = 0xFFFF;
  
  // Update GDT entry with TSS address
  uint64_t tss_address = (uint64_t)&tss;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.limit = sizeof(tss) - 1;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.base_low = tss_address & 0xFFFF;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.base_mid = (tss_address >> 16) & 0xFF;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.base_hi = (tss_address >> 24) & 0xFF;
  gdt_entries[GDT_TSS_SEGMENT / 8 + 1].sys_desc_upper.base_very_high = 
      (tss_address >> 32) & 0xFFFFFFFF;
  
  // Load TSS into TR register
  __asm__ volatile("ltr %%ax" : : "a"((uint16_t)GDT_TSS_SEGMENT));
  
  ktprintf("[TSS] Loaded with SP0=0x%llx\n", tss.sp0);
  ktprintf("[TSS] IST[%d] (Double Fault) = 0x%llx\n", 
            IST_DOUBLE_FAULT_STACK_INDEX, 
            tss.ist[IST_DOUBLE_FAULT_STACK_INDEX - 1]);
}

/**
 * Setups the GDT based on the needs of our operating system.
 *
 * Also sets all segment registers except SS and CS to zero.
 */
void gdt_init(void)
{
  // Add TSS to GDT
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.limit = sizeof(tss);
  const uint64_t tss_address = (uint64_t)&tss;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.base_low = tss_address & 0xFFFF;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.base_mid = (tss_address >> 16) & 0xFF;
  gdt_entries[GDT_TSS_SEGMENT / 8].normal.base_hi = (tss_address >> 24) & 0xFF;
  gdt_entries[GDT_TSS_SEGMENT / 8 + 1].sys_desc_upper.base_very_high =
      (tss_address >> 32) & 0xFFFFFFFF;
  struct gdtr gdt = {
      .limit = sizeof(gdt_entries) - 1,
      .ptr = (uint64_t)&gdt_entries[0],
  };
  reload_segments(&gdt);
  ktprintf("GDT initialized\n");
}

/**
 * Update TSS SP0 when switching processes
 * Called by scheduler before context switch to user
 */
void tss_set_kernel_stack(uint64_t stack_top) {
    tss.sp0 = stack_top;
}