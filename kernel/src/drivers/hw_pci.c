#include "hw_detect.h"
#include "device_manager.h"
#include <stdbool.h>

// PCI config space access
static inline uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | 
                                  (func << 8) | (offset & 0xFC) | 0x80000000);

    // Write to CONFIG_ADDRESS (0xCF8)
    __asm__ volatile("outl %%eax, %%dx" :: "a"(address), "d"(0xCF8));

    // Read from CONFIG_DATA (0xCFC)
    uint32_t data;
    __asm__ volatile("inl %%dx, %%eax" : "=a"(data) : "d"(0xCFC));

    return data;
}

static bool pci_device_exists(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t vendor_device = pci_read_config(bus, dev, func, 0);
    return (vendor_device != 0xFFFFFFFF) && ((vendor_device & 0xFFFF) != 0xFFFF);
}

int hw_detect_pci_scan(void) {
    int found = 0;
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                if (!pci_device_exists(bus, dev, func)) {
                    if (func == 0) break; // No device
                    continue;
                }
                
                uint32_t vendor_device = pci_read_config(bus, dev, func, 0);
                uint32_t class_rev = pci_read_config(bus, dev, func, 0x08);
                uint32_t header = pci_read_config(bus, dev, func, 0x0C);
                
                pci_device_info_t info = {
                    .vendor_id = vendor_device & 0xFFFF,
                    .device_id = (vendor_device >> 16) & 0xFFFF,
                    .class_code = (class_rev >> 24) & 0xFF,
                    .subclass = (class_rev >> 16) & 0xFF,
                    .prog_if = (class_rev >> 8) & 0xFF,
                    .bus = bus,
                    .device = dev,
                    .function = func,
                };
                
                // Read BARs
                for (int i = 0; i < 6; i++) {
                    info.bar[i] = pci_read_config(bus, dev, func, 0x10 + (i * 4));
                }
                
                // Read IRQ
                uint32_t irq_line = pci_read_config(bus, dev, func, 0x3C);
                info.irq = irq_line & 0xFF;
                
                device_register_from_pci(&info);
                found++;
                
                // If not multifunction, skip other functions
                if (func == 0 && !(header & 0x00800000)) {
                    break;
                }
            }
        }
    }
    
    return found;
}