// hw_detect.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver.h"

// Hardware detection results
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uintptr_t bar[6];  // Base Address Registers
} pci_device_info_t;

typedef struct {
    uint8_t port;      // 0x60 or 0x64
    uint8_t irq;       // IRQ1 or IRQ12
    bool exists;
} ps2_device_info_t;

// Discovery functions
void hw_detect_init(void);
int hw_detect_pci_scan(void);
int hw_detect_ps2_scan(void);
int hw_detect_platform_devices(void);

// Callbacks for enumeration
typedef void (*pci_enum_callback_t)(const pci_device_info_t* info, void* ctx);
typedef void (*ps2_enum_callback_t)(const ps2_device_info_t* info, void* ctx);

void hw_pci_enumerate(pci_enum_callback_t callback, void* ctx);
void hw_ps2_enumerate(ps2_enum_callback_t callback, void* ctx);