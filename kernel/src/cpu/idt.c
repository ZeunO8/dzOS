#include "idt.h"
#include "gdt.h"
#include "traps.h"
#include "common/printf.h"
#include "device/pic.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t dpl, uint8_t type_attr)
{
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE_SEGMENT; // 0x08
    idt[vector].ist         = 0;
    idt[vector].type_attr   = (uint8_t)((type_attr & 0xFF) | ((dpl & 0x3) << 5));
    idt[vector].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero        = 0;
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }

    idt_load();

    ktprintf("IDT initialized with %d entries\n", 256);
}

void idt_load(void)
{
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)&idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));
}

extern void isr_stub_33(void);
extern void isr_stub_44(void);

void setup_input_interrupts(void)
{
    idt_set_gate(T_IRQ0 + IRQ_KEYBOARD, (uint64_t)isr_stub_33, 0, 0x8E);
    idt_set_gate(T_IRQ0 + IRQ_MOUSE,    (uint64_t)isr_stub_44, 0, 0x8E);
}

void interrupt_dispatch(uint64_t vector)
{
    if (vector == T_IRQ0 + IRQ_KEYBOARD) {
        keyboard_handler();
    } else if (vector == T_IRQ0 + IRQ_MOUSE) {
        mouse_handler();
    } else {
        lapic_send_eoi();
    }
}
