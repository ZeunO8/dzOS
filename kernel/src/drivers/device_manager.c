// device_manager.c - Enhanced with special device registration
#include "device_manager.h"
#include "common/lib.h"
#include "mem/kmalloc.h"
#include "common/printf.h"
#include "drivers/hw_detect.h"
#include "limine.h"

static device_manager_t g_dm = {0};

extern void os_trust_init(void);
extern void register_builtin_drivers(void);

// Framebuffer request (external from kernel.c)
extern const struct limine_framebuffer_response* get_framebuffer_response(void);

void device_manager_init(void) {
    memset(&g_dm, 0, sizeof(g_dm));

    ktprintf("=== Device Manager Initialization ===\n");
    
    // Initialize hardware detection subsystem
    hw_detect_init();
    
    // Scan for hardware (this creates device_t objects)
    ktprintf("--- Hardware Detection Phase ---\n");
    hw_detect_ps2_scan();
    hw_detect_pci_scan();
    hw_detect_platform_devices();
    
    // Register framebuffer if available (special case - needs Limine data)
    const struct limine_framebuffer_response* fb_resp = get_framebuffer_response();
    if (fb_resp && fb_resp->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_resp->framebuffers[0];
        device_register_framebuffer(fb);
    }
    
    // Register all builtin drivers
    ktprintf("--- Driver Registration Phase ---\n");
    register_builtin_drivers();
    
    // Match devices to drivers and probe
    ktprintf("--- Device-Driver Matching Phase ---\n");
    device_manager_probe_all();
    
    // Initialize all matched devices
    ktprintf("--- Device Initialization Phase ---\n");
    device_manager_init_all();
    
    ktprintf("=== Device Manager Initialization Complete ===\n\n");
}

int device_register_from_pci(const pci_device_info_t* hw_info) {
    if (g_dm.device_count >= MAX_DEVICES) return -1;
    
    device_t* dev = &g_dm.devices[g_dm.device_count];
    
    // Map PCI class to driver class
    driver_class_t class_ = DRIVER_CLASS_MISC;
    switch (hw_info->class_code) {
        case 0x01: class_ = DRIVER_CLASS_BLOCK; break;  // Storage
        case 0x02: class_ = DRIVER_CLASS_NET; break;    // Network
        case 0x03: class_ = DRIVER_CLASS_DISPLAY; break; // Display
        case 0x09: class_ = DRIVER_CLASS_INPUT; break;  // Input
    }
    
    dev->class_ = class_;
    dev->bus = DRIVER_BUS_PCI;
    dev->irq = hw_info->irq;
    dev->initialized = false;
    dev->drv = NULL;
    dev->name = NULL;
    
    // Store PCI-specific info in os_data
    dev->os_data = kmalloc(sizeof(pci_device_info_t));
    if (dev->os_data) {
        memcpy(dev->os_data, hw_info, sizeof(pci_device_info_t));
    }
    
    g_dm.device_count++;
    return 0;
}

int device_register_from_ps2(const ps2_device_info_t* hw_info) {
    if (g_dm.device_count >= MAX_DEVICES) return -1;
    
    device_t* dev = &g_dm.devices[g_dm.device_count];
    
    dev->name = (hw_info->irq == 1) ? "ps2_keyboard" : "ps2_mouse";
    dev->class_ = DRIVER_CLASS_INPUT;
    dev->bus = DRIVER_BUS_PS2;
    dev->irq = hw_info->irq;
    dev->initialized = false;
    dev->drv = NULL;
    
    dev->os_data = kmalloc(sizeof(ps2_device_info_t));
    if (dev->os_data) {
        memcpy(dev->os_data, hw_info, sizeof(ps2_device_info_t));
    }
    
    g_dm.device_count++;
    return 0;
}

int device_register_platform(const char* name, driver_class_t class_) {
    if (g_dm.device_count >= MAX_DEVICES) return -1;
    
    device_t* dev = &g_dm.devices[g_dm.device_count];
    
    dev->name = name;
    dev->class_ = class_;
    dev->bus = DRIVER_BUS_PLATFORM;
    dev->irq = 0;
    dev->initialized = false;
    dev->drv = NULL;
    dev->os_data = NULL;
    dev->driver_data = NULL;
    
    g_dm.device_count++;
    return 0;
}

int device_register_framebuffer(struct limine_framebuffer *fb) {
    if (g_dm.device_count >= MAX_DEVICES) return -1;
    
    device_t* dev = &g_dm.devices[g_dm.device_count];
    
    dev->name = "framebuffer";
    dev->class_ = DRIVER_CLASS_DISPLAY;
    dev->bus = DRIVER_BUS_PLATFORM;
    dev->irq = 0;
    dev->initialized = false;
    dev->drv = NULL;
    dev->os_data = fb; // Pass framebuffer pointer directly
    dev->driver_data = NULL;
    
    g_dm.device_count++;
    
    ktprintf("[HW_DETECT] Found framebuffer %llux%llu @ %u bpp\n",
             fb->width, fb->height, (uint32_t)fb->bpp);
    
    return 0;
}

device_t* device_find_by_name(const char* name) {
    if (!name) return NULL;
    
    for (size_t i = 0; i < g_dm.device_count; i++) {
        device_t* dev = &g_dm.devices[i];
        if (dev->name && strcmp(dev->name, name) == 0) {
            return dev;
        }
    }
    
    return NULL;
}

int driver_register_verified(driver_t* drv) {
    if (g_dm.driver_count >= MAX_DEVICES) return -1;
    
    // TODO: Verify signature if manifest exists
    
    g_dm.drivers[g_dm.driver_count] = drv;
    g_dm.driver_count++;
    
    ktprintf("[DRIVER] Registered driver '%s'\n", drv->name);
    return 0;
}

int driver_unregister(driver_t* drv) {
    for (size_t i = 0; i < g_dm.driver_count; i++) {
        if (g_dm.drivers[i] == drv) {
            for (size_t j = i; j < g_dm.driver_count - 1; j++) {
                g_dm.drivers[j] = g_dm.drivers[j + 1];
            }
            g_dm.driver_count--;
            return 0;
        }
    }
    return -1;
}

int device_driver_match_and_bind(device_t* dev) {
    for (size_t i = 0; i < g_dm.driver_count; i++) {
        driver_t* drv = g_dm.drivers[i];
        
        // Check if driver matches device bus and class
        if (drv->bus != dev->bus  || drv->class_ != dev->class_)
            continue;
        
        // Found a match - attach driver
        dev->drv = drv;
        ktprintf("[DEVICE] Matched device '%s' to driver '%s'\n", 
                 dev->name ? dev->name : "unnamed", drv->name);
        return 0;
    }
    
    ktprintf("[DEVICE] No driver found for device '%s' (bus=%d, class=%d)\n",
             dev->name ? dev->name : "unnamed", dev->bus, dev->class_);
    return -1;
}

void device_manager_probe_all(void) {
    ktprintf("[DEVICE] Probing %zu devices...\n", g_dm.device_count);
    
    int matched = 0;
    for (size_t i = 0; i < g_dm.device_count; i++) {
        device_t* dev = &g_dm.devices[i];
        
        if (device_driver_match_and_bind(dev) == 0) {
            matched++;
            
            // Call probe if driver has one
            if (dev->drv && dev->drv->ops.probe) {
                int result = dev->drv->ops.probe(dev);
                if (result != 0) {
                    ktprintf("[DEVICE] Probe failed for '%s': %d\n", 
                             dev->name ? dev->name : "unnamed", result);
                    dev->drv = NULL;
                    matched--;
                }
            }
        }
    }
    
    ktprintf("[DEVICE] Matched %d/%zu devices to drivers\n", matched, g_dm.device_count);
}

void device_manager_init_all(void) {
    ktprintf("[DEVICE] Initializing devices...\n");
    
    int initialized = 0;
    for (size_t i = 0; i < g_dm.device_count; i++) {
        device_t* dev = &g_dm.devices[i];
        
        if (!dev->drv) continue;
        
        if (dev->drv->ops.init) {
            int result = dev->drv->ops.init(dev);
            if (result == 0) {
                dev->initialized = true;
                initialized++;
                ktprintf("[DEVICE] Initialized '%s'\n", 
                         dev->name ? dev->name : "unnamed");
            } else {
                ktprintf("[DEVICE] Init failed for '%s': %d\n",
                         dev->name ? dev->name : "unnamed", result);
            }
        } else {
            dev->initialized = true;
            initialized++;
        }
    }
    
    ktprintf("[DEVICE] Initialized %d devices\n", initialized);
}

device_t* device_find_by_irq(uint8_t irq) {
    for (size_t i = 0; i < g_dm.device_count; i++) {
        device_t* dev = &g_dm.devices[i];
        if (dev->irq == irq && dev->initialized) {
            return dev;
        }
    }
    return NULL;
}