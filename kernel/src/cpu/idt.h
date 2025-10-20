#pragma once
#include <stdint.h>

void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t dpl, uint8_t type_attr);
void idt_init(void);
void idt_load(void);
void idt_set_gate_with_ist(uint8_t vector, uint64_t handler, uint8_t ist, 
                            uint8_t dpl, uint8_t type_attr);
void idt_init_critical_handlers(void);