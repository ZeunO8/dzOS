// kernel.c - Updated for driver-based initialization
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

__attribute__((used, section(".limine_requests"))) static volatile LIMINE_BASE_REVISION(3);

__attribute__((
    used,
    section(".limine_requests"))
) static volatile struct limine_memmap_request
    memmap_request = {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
};

__attribute__((
    used,
    section(".limine_requests")
)) static volatile struct limine_hhdm_request
    hhdm_request = {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
};

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

__attribute__((
    used,
    section(".limine_requests")
)) static volatile struct limine_framebuffer_request
    framebuffer_request = {
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0,
};

const struct limine_framebuffer_response* get_framebuffer_response(void) {
    return framebuffer_request.response;
}

struct limine_framebuffer *get_framebuffer()
{
    return framebuffer_request.response->framebuffers[0];
}

static uint32_t rng_state = 2463534242U;

void srand_custom(uint32_t seed)
{
    if (seed == 0)
        seed = 1;
    rng_state = seed;
}

uint32_t rand_custom()
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

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

extern device_t* device_find_by_name(const char* name);
extern void rtc_set_global(device_t* dev);
extern void serial_set_global(device_t* dev);
extern void fb_set_global(device_t* dev);
extern void nvme_set_global(device_t* dev);

void kmain(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
        halt();

    fpu_enable();

    // Early serial init for debug output (before driver system)
    outb(0x3f8 + 2, 0);
    outb(0x3f8 + 3, 0b10000000);
    outb(0x3f8 + 0, 115200 / 9600);
    outb(0x3f8 + 1, 0);
    outb(0x3f8 + 3, 0b00000011);
    outb(0x3f8 + 4, 0);

    set_output_mode(OUTPUT_SERIAL);

    gdt_init();
    cpu_local_setup();

    // Memory initialization
    init_mem(hhdm_request.response->offset, memmap_request.response);

    // === EARLY DEVICE INITIALIZATION (RTC for timestamps) ===
    device_manager_early_init();
    
    vmm_init_kernel(*kernel_address_request.response);
    kmalloc_init();

    km_test();

    idt_init();
    tss_init_and_load();

    // Initialize interrupt controller
    ioapic_init(&rsdp_request);
    lapic_init();
    
    // Now we have timestamps! This will show actual time
    kprint_rtc_init_string();
    
    // === FULL DRIVER SYSTEM INITIALIZATION ===
    device_manager_init();
    
    // Enable interrupts after all drivers are initialized
    sti();

    // Find and set global pointers for legacy compatibility
    // (RTC is already set by early init)
    
    kprint_gdt_init_string();

    device_t* serial_dev = device_find_by_name("serial");
    if (serial_dev && serial_dev->initialized) {
        serial_set_global(serial_dev);
    }

    device_t* fb_dev = device_find_by_name("fb0");
    if (fb_dev && fb_dev->initialized) {
        fb_set_global(fb_dev);
    }

    device_t* nvme_dev = device_find_by_name("nvme0");
    if (nvme_dev && nvme_dev->initialized) {
        nvme_set_global(nvme_dev);
    } else {
        ktprintf("Warning: No NVMe device found!\n");
    }

    // Filesystem initialization (depends on NVMe)
    fs_init();

    // Userspace initialization
    scheduler_init();
    init_syscall_table();

    scheduler();

    halt();

}