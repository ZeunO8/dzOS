#include "common/lib.h"
#include "common/printf.h"
#include "common/fb.h"
#include "cpu/asm.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/smp.h"
#include "device/fb.h"
#include "device/nvme.h"
#include "device/pcie.h"
#include "device/pic.h"
#include "device/rtc.h"
#include "device/serial_port.h"
#include "fs/fs.h"
#include "limine.h"
#include "mem/mem.h"
#include "mem/vmm.h"
#include "userspace/proc.h"
#include "userspace/syscall.h"
#include "common/term.h"
#include "cpu/fpu.h"
#include "mem/kmalloc.h"
#include <stddef.h>
#include <stdint.h>

#include "drivers/device_manager.h"
#include "drivers/hw_detect.h"

__attribute__((used, section(".limine_requests"))) static volatile LIMINE_BASE_REVISION(3);

// Get the mapping of the memory to allocate free space for programs
__attribute__((
    used,
    section(".limine_requests"))
) static volatile struct limine_memmap_request
    memmap_request = {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
};

// Have a 1:1 map of physical memory on a high address
__attribute__((
    used,
    section(".limine_requests")
)) static volatile struct limine_hhdm_request
    hhdm_request = {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
};

// Get the physical address of kernel for trampoline page
__attribute__((
    used,
    section(".limine_requests")
)) static volatile struct limine_kernel_address_request
    kernel_address_request = {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
};

__attribute__((
    used,
    section(".limine_requests")
)) static volatile struct limine_rsdp_request
    rsdp_request = {
        .id = LIMINE_RSDP_REQUEST,
        .revision = 0,  
};

static uint32_t rng_state = 2463534242U; // Seed (can be any non-zero value)

// Seed the PRNG
void srand_custom(uint32_t seed)
{
    if (seed == 0)
        seed = 1; // zero seed breaks Xorshift
    rng_state = seed;
}
extern void os_trust_init(void);
extern void register_builtin_drivers(void);
// Generate a pseudo-random 32-bit number
uint32_t rand_custom()
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

// Generate a random number in range [0, max)
uint32_t rand_range(uint32_t max)
{
    return rand_custom() % max;
}

void km_test()
{
    void *a = kcmalloc(10);
    void *b = kcmalloc(12);
    void *c = kcmalloc(256);
    void *d = kcmalloc(4096);
    memset_int8_t(a, 0x70, 10);
    memset_int8_t(b, 0x47, 12);
    memset_int64_t(b, 0x12, 256);
    memset(b, 0x23, 4096);
    kmfree(a);
    kmfree(b);
    kmfree(c);
    kmfree(d);
}

void kmain(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
        halt();

    // os_trust_init();

    fpu_enable();

    rtc_init();

    if (serial_init() != 0)
        halt();

    set_output_mode(OUTPUT_SERIAL);

    if (init_framebuffer())
        panic("Unable to initialize flanterm!");
    
    init_term();

    // set_output_mode(OUTPUT_FLANTERM);

    gdt_init();

    cpu_local_setup();

    kprint_rtc_init_string();

    kprint_gdt_init_string();

    init_mem(hhdm_request.response->offset, memmap_request.response);
    vmm_init_kernel(*kernel_address_request.response);

    kmalloc_init();

    km_test();

    idt_init();

    tss_init_and_load();

    ioapic_init(&rsdp_request);
    lapic_init();

    setup_input_interrupts();

    

    ktprintf("\n=== Device Manager Initialization ===\n");
    
    // 1. Initialize device manager
    device_manager_init();
    
    // 2. Initialize hardware detection subsystem
    hw_detect_init();
    
    // 3. Scan for hardware (this creates device_t objects)
    ktprintf("\n--- Hardware Detection Phase ---\n");
    hw_detect_ps2_scan();
    hw_detect_pci_scan();
    hw_detect_platform_devices();
    
    // 4. Register all builtin drivers
    ktprintf("\n--- Driver Registration Phase ---\n");
    register_builtin_drivers();
    
    // 5. Match devices to drivers and probe
    ktprintf("\n--- Device-Driver Matching Phase ---\n");
    device_manager_probe_all();
    
    // 6. Initialize all matched devices
    ktprintf("\n--- Device Initialization Phase ---\n");
    device_manager_init_all();
    
    ktprintf("\n=== Device Manager Initialization Complete ===\n\n");



    sti();

    nvme_init();

    fs_init();

    scheduler_init();

    init_syscall_table();

    scheduler();

    halt();
}