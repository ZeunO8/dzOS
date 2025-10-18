#pragma once
#include <stdint.h>
#include <stdbool.h>

// PS/2 device info
typedef struct {
    uint16_t port;
    uint8_t irq;
    bool exists;
} ps2_device_info_t;

// PCI device info
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t irq;
    uint32_t bar[6];
} pci_device_info_t;

// Hardware detection functions
void hw_detect_init(void);
int hw_detect_ps2_scan(void);
int hw_detect_pci_scan(void);
int hw_detect_platform_devices(void);