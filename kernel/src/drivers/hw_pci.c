// hw_pci.c
#include "hw_detect.h"
#include "device_manager.h"
#include "cpu/asm.h"
#include "common/printf.h"
#include "mem/kmalloc.h"

// PCI class code definitions for better device identification
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_MEMORY        0x05
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_COMM          0x07
#define PCI_CLASS_SYSTEM        0x08
#define PCI_CLASS_INPUT         0x09
#define PCI_CLASS_DOCKING       0x0A
#define PCI_CLASS_PROCESSOR     0x0B
#define PCI_CLASS_SERIAL        0x0C

// Common subclass definitions
#define PCI_SUBCLASS_STORAGE_SCSI       0x00
#define PCI_SUBCLASS_STORAGE_IDE        0x01
#define PCI_SUBCLASS_STORAGE_FLOPPY     0x02
#define PCI_SUBCLASS_STORAGE_RAID       0x04
#define PCI_SUBCLASS_STORAGE_ATA        0x05
#define PCI_SUBCLASS_STORAGE_SATA       0x06
#define PCI_SUBCLASS_STORAGE_SAS        0x07
#define PCI_SUBCLASS_STORAGE_NVME       0x08

#define PCI_SUBCLASS_NETWORK_ETHERNET   0x00
#define PCI_SUBCLASS_NETWORK_TOKEN_RING 0x01
#define PCI_SUBCLASS_NETWORK_FDDI       0x02
#define PCI_SUBCLASS_NETWORK_ATM        0x03
#define PCI_SUBCLASS_NETWORK_ISDN       0x04
#define PCI_SUBCLASS_NETWORK_WIRELESS   0x80

#define PCI_SUBCLASS_DISPLAY_VGA        0x00
#define PCI_SUBCLASS_DISPLAY_XGA        0x01
#define PCI_SUBCLASS_DISPLAY_3D         0x02

#define PCI_SUBCLASS_BRIDGE_HOST        0x00
#define PCI_SUBCLASS_BRIDGE_ISA         0x01
#define PCI_SUBCLASS_BRIDGE_EISA        0x02
#define PCI_SUBCLASS_BRIDGE_PCI         0x04
#define PCI_SUBCLASS_BRIDGE_CARDBUS     0x07

#define PCI_SUBCLASS_SERIAL_USB         0x03
#define PCI_SUBCLASS_SERIAL_FIREWIRE    0x00
#define PCI_SUBCLASS_SERIAL_SMBUS       0x05

// PCI Configuration Space Registers
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_REVISION_ID         0x08
#define PCI_PROG_IF             0x09
#define PCI_SUBCLASS            0x0A
#define PCI_CLASS_CODE          0x0B
#define PCI_CACHE_LINE_SIZE     0x0C
#define PCI_LATENCY_TIMER       0x0D
#define PCI_HEADER_TYPE         0x0E
#define PCI_BIST                0x0F
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_BAR2                0x18
#define PCI_BAR3                0x1C
#define PCI_BAR4                0x20
#define PCI_BAR5                0x24
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_SUBSYSTEM_ID        0x2E
#define PCI_INTERRUPT_LINE      0x3C
#define PCI_INTERRUPT_PIN       0x3D

// Header type flags
#define PCI_HEADER_TYPE_MASK    0x7F
#define PCI_HEADER_MULTIFUNCTION 0x80

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

static uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func,
                                    uint8_t offset)
{
    uint16_t word = pci_config_read_word(bus, slot, func, offset & ~1);
    return (offset & 1) ? (word >> 8) : (word & 0xFF);
}

static void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func,
                                   uint8_t offset, uint32_t value)
{
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    uint32_t address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) |
                                  (offset & 0xFC) | ((uint32_t)0x80000000));

    bool interrupts_enabled = is_interrupts_enabled();
    cli();
    outl(0xCF8, address);
    outl(0xCFC, value);
    if (interrupts_enabled)
        sti();
}

static const char* pci_get_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case PCI_CLASS_STORAGE:
            switch (subclass) {
                case PCI_SUBCLASS_STORAGE_SCSI: return "SCSI Controller";
                case PCI_SUBCLASS_STORAGE_IDE: return "IDE Controller";
                case PCI_SUBCLASS_STORAGE_FLOPPY: return "Floppy Controller";
                case PCI_SUBCLASS_STORAGE_RAID: return "RAID Controller";
                case PCI_SUBCLASS_STORAGE_ATA: return "ATA Controller";
                case PCI_SUBCLASS_STORAGE_SATA: return "SATA Controller";
                case PCI_SUBCLASS_STORAGE_SAS: return "SAS Controller";
                case PCI_SUBCLASS_STORAGE_NVME: return "NVMe Controller";
                default: return "Storage Controller";
            }
        case PCI_CLASS_NETWORK:
            switch (subclass) {
                case PCI_SUBCLASS_NETWORK_ETHERNET: return "Ethernet Controller";
                case PCI_SUBCLASS_NETWORK_WIRELESS: return "Wireless Controller";
                default: return "Network Controller";
            }
        case PCI_CLASS_DISPLAY:
            switch (subclass) {
                case PCI_SUBCLASS_DISPLAY_VGA: return "VGA Controller";
                case PCI_SUBCLASS_DISPLAY_3D: return "3D Controller";
                default: return "Display Controller";
            }
        case PCI_CLASS_MULTIMEDIA:
            return "Multimedia Device";
        case PCI_CLASS_BRIDGE:
            switch (subclass) {
                case PCI_SUBCLASS_BRIDGE_HOST: return "Host Bridge";
                case PCI_SUBCLASS_BRIDGE_ISA: return "ISA Bridge";
                case PCI_SUBCLASS_BRIDGE_PCI: return "PCI Bridge";
                default: return "Bridge";
            }
        case PCI_CLASS_SERIAL:
            switch (subclass) {
                case PCI_SUBCLASS_SERIAL_USB: return "USB Controller";
                case PCI_SUBCLASS_SERIAL_FIREWIRE: return "FireWire Controller";
                default: return "Serial Controller";
            }
        default:
            return "Unknown Device";
    }
}

// Get BAR size by writing all 1s and reading back
static uint32_t pci_get_bar_size(uint8_t bus, uint8_t device, uint8_t func, 
                                  uint8_t bar_offset, uint32_t original_bar) {
    // Save original value
    uint32_t bar = original_bar;
    
    // Write all 1s
    pci_config_write_dword(bus, device, func, bar_offset, 0xFFFFFFFF);
    
    // Read back
    uint32_t size_mask = pci_config_read_dword(bus, device, func, bar_offset);
    
    // Restore original value
    pci_config_write_dword(bus, device, func, bar_offset, bar);
    
    // Calculate size
    if (size_mask == 0 || size_mask == 0xFFFFFFFF)
        return 0;
    
    // Check if it's an IO BAR
    if (bar & 0x1) {
        size_mask &= 0xFFFFFFFC;
    } else {
        size_mask &= 0xFFFFFFF0;
    }
    
    return ~size_mask + 1;
}

static void pci_scan_function(uint8_t bus, uint8_t device, uint8_t func, int* found) {
    uint16_t vendor = pci_config_read_word(bus, device, func, PCI_VENDOR_ID);
    
    // Check if device exists
    if (vendor == 0xFFFF)
        return;
    
    // Read device info
    uint16_t device_id = pci_config_read_word(bus, device, func, PCI_DEVICE_ID);
    uint8_t class_code = pci_config_read_byte(bus, device, func, PCI_CLASS_CODE);
    uint8_t subclass = pci_config_read_byte(bus, device, func, PCI_SUBCLASS);
    uint8_t prog_if = pci_config_read_byte(bus, device, func, PCI_PROG_IF);
    uint8_t irq = pci_config_read_byte(bus, device, func, PCI_INTERRUPT_LINE);
    uint8_t irq_pin = pci_config_read_byte(bus, device, func, PCI_INTERRUPT_PIN);
    uint16_t subsys_vendor = pci_config_read_word(bus, device, func, PCI_SUBSYSTEM_VENDOR_ID);
    uint16_t subsys_id = pci_config_read_word(bus, device, func, PCI_SUBSYSTEM_ID);
    
    // Read BARs
    uint32_t bar[6];
    for (int i = 0; i < 6; i++) {
        bar[i] = pci_config_read_dword(bus, device, func, PCI_BAR0 + (i * 4));
    }
    
    // Create device info structure
    pci_device_info_t* info = kmalloc(sizeof(pci_device_info_t));
    if (!info) return;
    
    info->bus = bus;
    info->device = device;
    info->function = func;
    info->vendor_id = vendor;
    info->device_id = device_id;
    info->class_code = class_code;
    info->subclass = subclass;
    info->prog_if = prog_if;
    info->irq = irq;
    info->irq_pin = irq_pin;
    info->subsystem_vendor = subsys_vendor;
    info->subsystem_id = subsys_id;
    
    for (int i = 0; i < 6; i++) {
        info->bar[i] = bar[i];
        info->bar_size[i] = pci_get_bar_size(bus, device, func, PCI_BAR0 + (i * 4), bar[i]);
    }
    
    const char* dev_type = pci_get_class_name(class_code, subclass);
    
    // Special logging for important device types
    if (class_code == PCI_CLASS_STORAGE && subclass == PCI_SUBCLASS_STORAGE_NVME && prog_if == 0x02) {
        uint64_t nvme_bar = (((uint64_t)bar[1] << 32) | (bar[0] & 0xFFFFFFF0));
        ktprintf("[HW_DETECT] Found %s at %p (vendor=0x%04x device=0x%04x) [%u.%u.%u]\n",
                 dev_type, nvme_bar, vendor, device_id, bus, device, func);
    } 
    else if (class_code == PCI_CLASS_NETWORK && subclass == PCI_SUBCLASS_NETWORK_ETHERNET) {
        ktprintf("[HW_DETECT] Found %s (vendor=0x%04x device=0x%04x) [%u.%u.%u]\n",
                 dev_type, vendor, device_id, bus, device, func);
    }
    else if (class_code == PCI_CLASS_DISPLAY) {
        ktprintf("[HW_DETECT] Found %s (vendor=0x%04x device=0x%04x) [%u.%u.%u]\n",
                 dev_type, vendor, device_id, bus, device, func);
    }
    else if (class_code == PCI_CLASS_SERIAL && subclass == PCI_SUBCLASS_SERIAL_USB) {
        ktprintf("[HW_DETECT] Found %s (vendor=0x%04x device=0x%04x) [%u.%u.%u]\n",
                 dev_type, vendor, device_id, bus, device, func);
    }
    else {
        ktprintf("[HW_DETECT] Found %s (vendor=0x%04x device=0x%04x class=0x%02x/0x%02x/0x%02x) [%u.%u.%u]\n",
                 dev_type, vendor, device_id, class_code, subclass, prog_if, bus, device, func);
    }
    
    device_register_from_pci(info);
    kmfree(info);
    (*found)++;
}

static void pci_scan_device(uint8_t bus, uint8_t device, int* found) {
    uint16_t vendor = pci_config_read_word(bus, device, 0, PCI_VENDOR_ID);
    
    // Check if device exists
    if (vendor == 0xFFFF)
        return;
    
    // Scan function 0
    pci_scan_function(bus, device, 0, found);
    
    // Check if it's a multi-function device
    uint8_t header_type = pci_config_read_byte(bus, device, 0, PCI_HEADER_TYPE);
    if (header_type & PCI_HEADER_MULTIFUNCTION) {
        // Scan all 8 functions
        for (uint8_t func = 1; func < 8; func++) {
            vendor = pci_config_read_word(bus, device, func, PCI_VENDOR_ID);
            if (vendor != 0xFFFF) {
                pci_scan_function(bus, device, func, found);
            }
        }
    }
}

int hw_detect_pci_scan(void) {
    ktprintf("[HW_DETECT] Scanning PCI bus...\n");
    
    int found = 0;
    
    // Scan all buses (0-255)
    // For now, just scan bus 0, but you can expand this
    for (uint8_t bus = 0; bus < 1; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            pci_scan_device(bus, device, &found);
        }
    }
    
    ktprintf("[HW_DETECT] Found %d PCI device(s)\n", found);
    return found;
}