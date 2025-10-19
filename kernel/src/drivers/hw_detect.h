// hw_detect.h
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
    uint8_t irq_pin;            // Interrupt pin (INTA#, INTB#, etc.)
    uint16_t subsystem_vendor;  // Subsystem vendor ID
    uint16_t subsystem_id;      // Subsystem ID
    uint32_t bar[6];
    uint32_t bar_size[6];       // Size of each BAR
} pci_device_info_t;

// Hardware detection functions
void hw_detect_init(void);
int hw_detect_ps2_scan(void);
int hw_detect_pci_scan(void);
int hw_detect_platform_devices(void);