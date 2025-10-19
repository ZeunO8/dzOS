#include "pic.h"
#include "common/lib.h"
#include "cpu/asm.h"
#include "cpu/smp.h"
#include "mem/mem.h"
#include "mem/vmm.h"
#include "common/printf.h"
#include "limine.h"

struct ioapic_mmio {
    uint32_t ioregsel;
    uint32_t pad[3];
    uint32_t iowin;
};

volatile struct ioapic_mmio *ioapic_mmio = NULL;
static uint32_t ioapic_gsi_base = 0;
static uint64_t ioapic_phys = 0;

/* IOAPIC low-level primitives. These require ioapic_mmio to be mapped. */
uint32_t ioapic_read(uint8_t reg)
{
    if (!ioapic_mmio) return 0;
    volatile uint32_t *base = (volatile uint32_t *)ioapic_mmio;
    base[0] = reg;                 // IOREGSEL at offset 0
    uint32_t val = base[4];        // IOWIN at offset 0x10 -> index 4
    return val;
}

uint32_t ioapic_read_public(uint8_t reg)
{
    return ioapic_read(reg);
}

void ioapic_write(uint8_t reg, uint32_t data)
{
    if (!ioapic_mmio) return;
    volatile uint32_t *base = (volatile uint32_t *)ioapic_mmio;
    base[0] = reg;
    base[4] = data;
}

/* Helper: compute redirection table index from GSI (irq). */
static inline int ioapic_redtbl_index_for_irq(int irq)
{
    int gsi = irq;
    if (gsi < (int)ioapic_gsi_base) {
        // caller expects irq to be GSI; if it's less than base, assume it's already index
        return irq;
    }
    return gsi - (int)ioapic_gsi_base;
}

/* Find IOAPIC physical address and gsi_base from MADT entries (type 1) */
static void find_ioapic_from_madt(void)
{
    extern struct MADT *madt; // from pic.c parse_madt()
    if (!madt) return;
    uint8_t *p = madt->entries;
    uint8_t *end = (uint8_t*)madt + madt->header.Length;
    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len = p[1];
        if (len < 2) break;
        if (type == 1 && (p + len) <= end) { // IOAPIC
            struct {
                uint8_t type;
                uint8_t length;
                uint8_t ioapic_id;
                uint8_t reserved;
                uint32_t ioapic_addr;
                uint32_t gsi_base;
            } __attribute__((packed)) *ent = (void*)p;
            ioapic_phys = ent->ioapic_addr;
            ioapic_gsi_base = ent->gsi_base;
            ktprintf("Found IOAPIC @ 0x%08x id=%u gsi_base=%u\n", ent->ioapic_addr, ent->ioapic_id, ent->gsi_base);
            return;
        }
        p += len;
    }
}

/* Initialize IOAPIC: find via MADT and map it, then mask all entries. */
void ioapic_init(volatile struct limine_rsdp_request* rsdp_request)
{
    uint64_t lapic_addr = parse_madt(rsdp_request);
    if (!lapic_addr) {
        panic("IOAPIC: init failed: no MADT found\n");
    }

    // find IOAPIC entry in the MADT
    find_ioapic_from_madt();

    if (!ioapic_phys) {
        ktprintf("No IOAPIC in MADT, falling back to 0x%llx\n", (unsigned long long)IOAPIC_FALLBACK_PHYS);
        ioapic_phys = IOAPIC_FALLBACK_PHYS;
        ioapic_gsi_base = 0;
    }

    // Map IOAPIC physical page into kernel address space
    ioapic_mmio = (volatile struct ioapic_mmio *)vmm_map_physical(ioapic_phys, ioapic_phys + 0x1000);
    if (!ioapic_mmio) {
        panic("IOAPIC: failed to map IOAPIC MMIO\n");
    }

    // Read version to get max redirection entries
    uint32_t ver = ioapic_read(IOAPIC_REG_VER);
    int maxred = ((ver >> 16) & 0xFF);
    ktprintf("IOAPIC ver 0x%x maxred=%d gsi_base=%u\n", ver, maxred, ioapic_gsi_base);

    // Mask all redirection entries initially
    for (int i = 0; i <= maxred; i++) {
        uint8_t low_index = IOAPIC_REDTBL_LOW(i);
        ioapic_write(low_index, IOAPIC_MASK_MASK | (T_IRQ0 + i));
        ioapic_write(IOAPIC_REDTBL_HIGH(i), 0);
    }

    // Fully disable legacy PICs
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    ktprintf("IOAPIC initialized (phys: 0x%llx gsi_base=%u maxred=%d)\n", (unsigned long long)ioapic_phys, ioapic_gsi_base, maxred);
}

/* Enable an IRQ: program redirection entry for given irq (GSI or irq number).
   irq parameter should be the GSI (e.g., IRQ_MOUSE + ioapic_gsi_base) or the plain IRQ if gsi_base==0.
*/
void ioapic_enable(int irq, int lapic_id)
{
    if (!ioapic_mmio) {
        ktprintf("ioapic_enable: ioapic not mapped\n");
        return;
    }

    int idx = ioapic_redtbl_index_for_irq(irq);
    uint8_t vector = (uint8_t)(T_IRQ0 + irq);

    uint32_t low = 0;
    low |= (uint32_t)vector;                    // vector
    low |= IOAPIC_DELIVERY_FIXED;
    low |= IOAPIC_DESTMODE_PHYS;
    low |= IOAPIC_POLARITY_LOW;                 // PS/2 typically active low
    low |= IOAPIC_TRIGGER_EDGE;                 // PS/2 is usually edge-triggered
    low &= ~IOAPIC_MASK_MASK;                   // ensure not masked

    uint32_t high = (lapic_id & 0xFF) << 24;

    ioapic_write(IOAPIC_REDTBL_LOW(idx), low);
    ioapic_write(IOAPIC_REDTBL_HIGH(idx), high);

    uint32_t low_read = ioapic_read(IOAPIC_REDTBL_LOW(idx));
    uint32_t high_read = ioapic_read(IOAPIC_REDTBL_HIGH(idx));
    ktprintf("IOAPIC enable irq=%d idx=%d low=0x%08x high=0x%08x\n", irq, idx, low_read, high_read);
}

void ioapic_disable(int irq)
{
    if (!ioapic_mmio) return;
    int idx = ioapic_redtbl_index_for_irq(irq);
    uint32_t low = ioapic_read(IOAPIC_REDTBL_LOW(idx));
    low |= IOAPIC_MASK_MASK;
    ioapic_write(IOAPIC_REDTBL_LOW(idx), low);
}

/* Get APIC base from MSR */
#define IA32_APIC_BASE_MSR 0x1B
static uintptr_t cpu_get_apic_base(void) {
  uint64_t msr = rdmsr(IA32_APIC_BASE_MSR);
  return msr & 0xfffff000;
}

/* Initialize local APIC */
void lapic_init(void)
{
    uint64_t apic_msr = cpu_get_apic_base();
    vmm_init_lapic(apic_msr);
    cpu_local()->lapic = (volatile uint32_t *)P2V(apic_msr);
    apic_msr |= (1UL << 11);
    wrmsr(IA32_APIC_BASE_MSR, apic_msr);

    lapic_write(LAPIC_SVR, (SPURIOUS_VECTOR | LAPIC_SVR_ENABLE));

    lapic_write(LAPIC_LINT0, LAPIC_MASKED);
    lapic_write(LAPIC_LINT1, LAPIC_MASKED);
    lapic_write(LAPIC_ERROR, LAPIC_MASKED);
    lapic_write(LAPIC_TIMER, LAPIC_MASKED);
    lapic_write(LAPIC_PCINT, LAPIC_MASKED);

    lapic_write(LAPIC_ESR, 0);
    lapic_read(LAPIC_ESR);

    lapic_write(LAPIC_EOI, 0);
    lapic_write(LAPIC_TPR, 0);

    ktprintf("LAPIC initialized (ID: %d)\n", lapic_read(LAPIC_ID) >> 24);
}

void lapic_write(uint32_t off, uint32_t val)
{
    volatile uint32_t* lapic = cpu_local()->lapic;
    lapic[off/4] = val;
    (void)lapic[LAPIC_ID/4];
}

uint32_t lapic_read(uint32_t off)
{
    volatile uint32_t* lapic = cpu_local()->lapic;
    return lapic[off/4];
}

void lapic_send_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void)
{
    return lapic_read(LAPIC_ID) >> 24;
}

/* MADT parsing: adapted to find MADT pointer - parse_madt already implemented below */
struct MADT *madt = NULL;

static void *find_sdt(const char *sig, void *rsdt_base, int is_xsdt) {
    struct SDTHeader *rsdt = (struct SDTHeader *)rsdt_base;
    int entry_count = (rsdt->Length - sizeof(struct SDTHeader)) / (is_xsdt ? 8 : 4);
    for (int i = 0; i < entry_count; i++) {
        uint64_t entry_addr = is_xsdt
            ? ((uint64_t *)((uintptr_t)rsdt + sizeof(struct SDTHeader)))[i]
            : ((uint32_t *)((uintptr_t)rsdt + sizeof(struct SDTHeader)))[i];
        struct SDTHeader *hdr = (struct SDTHeader *)vmm_map_physical(entry_addr, entry_addr + 0x1000);
        if (!memcmp(hdr->Signature, sig, 4)) return hdr;
    }
    return NULL;
}

/* parse_madt returns LAPIC base for logging and sets internal madt pointer */
uint64_t parse_madt(volatile struct limine_rsdp_request* rsdp_request) {
    uint64_t rsdp_phys = (uint64_t)rsdp_request->response->address;
    if (!rsdp_phys) {
        ktprintf("No RSDP provided by Limine!\n");
        return 0;
    }

    struct RSDPDescriptor20 *rsdp = (struct RSDPDescriptor20 *)vmm_map_physical(rsdp_phys, rsdp_phys + 0x1000);

    void *root_table = NULL;
    int is_xsdt = 0;
    if (rsdp->firstPart.Revision >= 2 && rsdp->XsdtAddress) {
        root_table = vmm_map_physical(rsdp->XsdtAddress, rsdp->XsdtAddress + 0x1000);
        is_xsdt = 1;
    } else {
        root_table = vmm_map_physical(rsdp->firstPart.RsdtAddress, rsdp->firstPart.RsdtAddress + 0x1000);
    }

    struct MADT *found = (struct MADT *)find_sdt("APIC", root_table, is_xsdt);
    if (!found) {
        ktprintf("No MADT found!\n");
        return 0;
    }

    madt = found;
    ktprintf("MADT located at %p (LAPIC addr: 0x%x)\n", madt, madt->lapic_addr);
    return (uint64_t)madt->lapic_addr;
}