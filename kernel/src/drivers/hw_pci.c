#include "hw_detect.h"
#include "device_manager.h"
#include "cpu/asm.h"
#include "common/printf.h"
#include "mem/kmalloc.h"

static uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func,
                                     uint8_t offset)
{
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    uint32_t address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                                  (offset & 0xFC) | ((uint32_t)0x80000000));

    bool interrupts_enabled = is_interrupts_enabled();
    cli();
    outl(0xCF8, address);
    uint16_t result = (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
    if (interrupts_enabled)
        sti();
    return result;
}

static uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func,
                                      uint8_t offset)
{
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    uint32_t address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                                  (offset & 0xFC) | ((uint32_t)0x80000000));

    bool interrupts_enabled = is_interrupts_enabled();
    cli();
    outl(0xCF8, address);
    uint32_t result = inl(0xCFC);
    if (interrupts_enabled)
        sti();
    return result;
}

int hw_detect_pci_scan(void) {
    ktprintf("[HW_DETECT] Scanning PCI bus...\n");
    
    int found = 0;
    
    // Scan bus 0 only for now (we can expand to all buses later)
    for (uint8_t device = 0; device < 32; device++) {
        uint16_t vendor = pci_config_read_word(0, device, 0, 0);
        
        // Check if device exists
        if (vendor == 0xFFFF)
            continue;
        
        // Read device info
        uint16_t device_id = pci_config_read_word(0, device, 0, 2);
        uint16_t class_subclass = pci_config_read_word(0, device, 0, 0xA);
        uint8_t class_code = (class_subclass >> 8) & 0xFF;
        uint8_t subclass = class_subclass & 0xFF;
        uint8_t prog_if = (pci_config_read_word(0, device, 0, 0x8) >> 8) & 0xFF;
        uint8_t irq = pci_config_read_word(0, device, 0, 0x3C) & 0xFF;
        
        // Read BARs
        uint32_t bar0 = pci_config_read_dword(0, device, 0, 0x10);
        uint32_t bar1 = pci_config_read_dword(0, device, 0, 0x14);
        uint32_t bar2 = pci_config_read_dword(0, device, 0, 0x18);
        uint32_t bar3 = pci_config_read_dword(0, device, 0, 0x1C);
        uint32_t bar4 = pci_config_read_dword(0, device, 0, 0x20);
        uint32_t bar5 = pci_config_read_dword(0, device, 0, 0x24);
        
        // Create device info structure
        pci_device_info_t* info = kmalloc(sizeof(pci_device_info_t));
        if (!info) continue;
        
        info->bus = 0;
        info->device = device;
        info->function = 0;
        info->vendor_id = vendor;
        info->device_id = device_id;
        info->class_code = class_code;
        info->subclass = subclass;
        info->prog_if = prog_if;
        info->irq = irq;
        info->bar[0] = bar0;
        info->bar[1] = bar1;
        info->bar[2] = bar2;
        info->bar[3] = bar3;
        info->bar[4] = bar4;
        info->bar[5] = bar5;
        
        // Special logging for NVMe
        if (class_code == 0x01 && subclass == 0x08 && prog_if == 0x02) {
            uint64_t nvme_bar = (((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0));
            ktprintf("[HW_DETECT] Found NVMe controller at %p (vendor=0x%x device=0x%x)\n",
                     nvme_bar, vendor, device_id);
        } else {
            ktprintf("[HW_DETECT] Found PCI device %u.%u (vendor=0x%x device=0x%x class=0x%02x/0x%02x/0x%02x)\n",
                     0, device, vendor, device_id, class_code, subclass, prog_if);
        }
        
        device_register_from_pci(info);
        kmfree(info);
        found++;
    }
    
    ktprintf("[HW_DETECT] Found %d PCI devices\n", found);
    return found;
}