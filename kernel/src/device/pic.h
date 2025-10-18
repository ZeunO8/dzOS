#pragma once
#include <stdint.h>
#include <stddef.h>

#include "limine.h"

#define IOAPIC_FALLBACK_PHYS 0xFEC00000UL

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define ICW1_INIT    0x10
#define ICW1_ICW4    0x01
#define ICW4_8086    0x01
#define PIC_EOI      0x20

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define T_IRQ0 32
#define IRQ_KEYBOARD 1
#define IRQ_MOUSE    12

// IOAPIC redirection table register helpers (reg indexes)
#define IOAPIC_REDTBL_LOW(i)  (0x10 + (i) * 2)
#define IOAPIC_REDTBL_HIGH(i) (0x10 + (i) * 2 + 1)

// IOAPIC register indices
#define IOAPIC_REG_ID   0x00
#define IOAPIC_REG_VER  0x01
#define IOAPIC_REG_ARB  0x02
#define IOAPIC_REDTBL_BASE 0x10

// IOAPIC redirection bit masks
#define IOAPIC_MASK_MASK        (1u << 16)
#define IOAPIC_MASK_UNMASK      (0u << 16)
#define IOAPIC_TRIGGER_LEVEL    (1u << 15)
#define IOAPIC_TRIGGER_EDGE     (0u << 15)
#define IOAPIC_POLARITY_LOW     (1u << 13)
#define IOAPIC_POLARITY_HIGH    (0u << 13)
#define IOAPIC_DESTMODE_LOGICAL (1u << 11)
#define IOAPIC_DESTMODE_PHYS    (0u << 11)
#define IOAPIC_DELIVERY_FIXED   (0u << 8)

// Local APIC register offsets (byte offsets)
#define LAPIC_ID      0x020
#define LAPIC_VER     0x030
#define LAPIC_TPR     0x080
#define LAPIC_EOI     0x0B0
#define LAPIC_SVR     0x0F0
#define LAPIC_ESR     0x280
#define LAPIC_ICRLO   0x300
#define LAPIC_ICRHI   0x310
#define LAPIC_TIMER   0x320
#define LAPIC_PCINT   0x340
#define LAPIC_LINT0   0x350
#define LAPIC_LINT1   0x360
#define LAPIC_ERROR   0x370
#define LAPIC_TICR    0x380
#define LAPIC_TCCR    0x390
#define LAPIC_TDCR    0x3E0

// LAPIC helper masks (used directly as values written to regs)
#define LAPIC_SVR_ENABLE   0x00000100
#define LAPIC_MASKED       0x00010000
#define LAPIC_PERIODIC     0x00020000
#define LAPIC_X1_DIV       0x0000000B

// Simple IOAPIC/LAPIC helpers in pic.c
void ioapic_init(volatile struct limine_rsdp_request* rsdp_request);
void ioapic_enable(int irq, int cpunum);
void ioapic_disable(int irq);
uint32_t ioapic_read(uint8_t reg);
uint32_t ioapic_read_public(uint8_t reg);
void ioapic_write(uint8_t reg, uint32_t val);

void lapic_init(void);
void lapic_write(uint32_t off, uint32_t val);
uint32_t lapic_read(uint32_t off);
void lapic_send_eoi(void);
uint32_t lapic_get_id(void);

uint64_t parse_madt(volatile struct limine_rsdp_request* rsdp_request);

// Basic ACPI structures
struct RSDPDescriptor {
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RsdtAddress;
} __attribute__((packed));

struct RSDPDescriptor20 {
    struct RSDPDescriptor firstPart;
    uint32_t Length;
    uint64_t XsdtAddress;
    uint8_t ExtendedChecksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct SDTHeader {
    char Signature[4];
    uint32_t Length;
    uint8_t Revision;
    uint8_t Checksum;
    char OEMID[6];
    char OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
} __attribute__((packed));

struct MADT {
    struct SDTHeader header;
    uint32_t lapic_addr;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed));

#define SPURIOUS_VECTOR 0xEF