// idt.c
#include "drivers/device_manager.h"
#include "idt.h"
#include "gdt.h"
#include "traps.h"
#include "common/printf.h"
#include "device/pic.h"
#include "userspace/proc.h"

void handle_page_fault(uint64_t error_code, uint64_t faulting_address)
{
	// Check if this is a stack overflow
	struct process *p = my_process();
	if (p) {
		uint64_t guard_page_start = p->kernel_stack_base - KERNEL_STACK_GUARD_SIZE;
		uint64_t guard_page_end = p->kernel_stack_base;
		
		if (faulting_address >= guard_page_start && faulting_address < guard_page_end) {
			// Stack overflow detected!
			kprintf("\n");
			kprintf("================================================\n");
			kprintf("KERNEL PANIC: Stack Overflow Detected!\n");
			kprintf("================================================\n");
			kprintf("Process: PID %llu\n", p->pid);
			kprintf("Faulting address: 0x%llx (guard page)\n", faulting_address);
			kprintf("Stack range: 0x%llx - 0x%llx\n", 
			        p->kernel_stack_base, p->kernel_stack_top);
			kprintf("Error code: 0x%llx\n", error_code);
			kprintf("================================================\n");
			panic("Stack overflow - increase KERNEL_STACK_SIZE");
		}
	}
	
	// Handle other page faults normally
	kprintf("Page fault at 0x%llx (error: 0x%llx)\n", faulting_address, error_code);
	panic("Unhandled page fault");
}

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

void interrupt_dispatch(uint64_t vector)
{
    uint8_t irq = vector - T_IRQ0;  // map vector to IRQ number
    device_t* dev = device_find_by_irq(irq);

    if (dev && dev->drv && dev->drv->ops.irq_handler) {
        driver_irq(dev, irq);  // safe inline call helper
    } else {
        ktprintf("[INT] Unhandled IRQ %u (vector %llu)\n", irq, vector);
    }

    lapic_send_eoi();
}

/**
 * Set IDT gate with IST support for critical interrupts
 */
void idt_set_gate_with_ist(uint8_t vector, uint64_t handler, uint8_t ist, 
                            uint8_t dpl, uint8_t type_attr)
{
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE_SEGMENT;
    idt[vector].ist         = ist;  // IST index (1-7, or 0 for normal stack)
    idt[vector].type_attr   = (uint8_t)((type_attr & 0xFF) | ((dpl & 0x3) << 5));
    idt[vector].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].zero        = 0;
}

/**
 * Initialize critical interrupt handlers with IST
 */
void idt_init_critical_handlers(void) {
    // // Double fault uses IST[0] (separate stack)
    // extern void isr_double_fault(void);
    // idt_set_gate_with_ist(T_DBLFLT, (uint64_t)isr_double_fault, 
    //                       IST_DOUBLE_FAULT_STACK_INDEX, 0, 0x8E);
    
    // // NMI uses IST[1]
    // extern void isr_nmi(void);
    // idt_set_gate_with_ist(T_NMI, (uint64_t)isr_nmi, 
    //                       IST_NMI_STACK_INDEX, 0, 0x8E);
    
    // // Machine check uses IST[2]
    // extern void isr_machine_check(void);
    // idt_set_gate_with_ist(T_MCHK, (uint64_t)isr_machine_check, 
    //                       IST_MACHINE_CHECK_STACK_INDEX, 0, 0x8E);
    
    // // Debug/Breakpoint uses IST[3]
    // extern void isr_debug(void);
    // idt_set_gate_with_ist(T_DEBUG, (uint64_t)isr_debug, 
    //                       IST_DEBUG_STACK_INDEX, 0, 0x8E);
    
    // ktprintf("[IDT] Critical handlers installed with IST\n");
}