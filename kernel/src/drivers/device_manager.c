// device_manager.c - Enhanced with special device registration
#include "device_manager.h"
#include "common/lib.h"
#include "mem/kmalloc.h"
#include "common/printf.h"
#include "drivers/hw_detect.h"
#include "limine.h"

static device_manager_t g_dm = {0};

extern void register_rtc_driver(void);
extern void register_serial_driver(void);
extern void rtc_set_global(device_t* dev);

extern void os_trust_init(void);
extern void register_builtin_drivers(void);

// Framebuffer request (external from kernel.c)
extern const struct limine_framebuffer_response* get_framebuffer_response(void);

// Register only critical early-boot drivers
void register_early_drivers(void) {
    ktprintf("[DRIVER] Registering early drivers...\n");
    register_rtc_driver();
    // Optionally register serial here if you need it early
    // register_serial_driver();
    ktprintf("[DRIVER] Early drivers registered\n");
}

// Early init - called before main device manager init
void device_manager_early_init(void) {
    memset(&g_dm, 0, sizeof(g_dm));
    
    ktprintf("=== Early Device Manager Initialization ===\n");

    hw_detect_platform_devices();
    
    // Register early drivers (RTC driver)
    register_early_drivers();
    
    // Find and match RTC device
    device_t* rtc_dev = device_find_by_name("rtc");
    if (rtc_dev) {
        if (device_driver_match_and_bind(rtc_dev) == 0) {
            // Probe
            if (rtc_dev->drv && rtc_dev->drv->ops.probe) {
                rtc_dev->drv->ops.probe(rtc_dev);
            }
            
            // Initialize
            if (rtc_dev->drv && rtc_dev->drv->ops.init) {
                if (rtc_dev->drv->ops.init(rtc_dev) == 0) {
                    rtc_dev->initialized = true;
                    rtc_set_global(rtc_dev);
                    ktprintf("[EARLY] RTC initialized successfully\n");
                }
            }
        }
    }
    
    g_dm.initialized = true;
    
    ktprintf("=== Early Device Manager Initialization Complete ===\n");
}

void device_manager_init(void) {
    // Skip device manager initialization if already done early
    if (g_dm.initialized) {
        ktprintf("=== Device Manager (Full Initialization) ===\n");
    } else {
        memset(&g_dm, 0, sizeof(g_dm));
        ktprintf("=== Device Manager Initialization ===\n");
    }
    
    // Initialize hardware detection subsystem
    hw_detect_init();
    
    // Scan for hardware (this creates device_t objects)
    ktprintf("--- Hardware Detection Phase ---\n");
    hw_detect_ps2_scan();
    hw_detect_pci_scan();
    
    // Register framebuffer if available
    const struct limine_framebuffer_response* fb_resp = get_framebuffer_response();
    if (fb_resp) {
        ktprintf("[HW_DETECT] Found %i framebuffer(s)\n", fb_resp->framebuffer_count);
        for (int i = 0; i < fb_resp->framebuffer_count; i++)
            device_register_framebuffer(fb_resp->framebuffers[i]);
    }
    
    // Register all builtin drivers (skip if early init already registered some)
    ktprintf("--- Driver Registration Phase ---\n");
    register_builtin_drivers();
    
    // Match devices to drivers and probe
    ktprintf("--- Device-Driver Matching Phase ---\n");
    device_manager_probe_all();
    
    // Initialize all matched devices
    ktprintf("--- Device Initialization Phase ---\n");
    device_manager_init_all();
    
    ktprintf("=== Device Manager Initialization Complete ===\n");
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
    
    // Check if device already exists
    device_t* existing = device_find_by_name(name);
    if (existing) {
        return 0; // Already registered
    }
    
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
    
    // Check if driver already registered
    for (size_t i = 0; i < g_dm.driver_count; i++) {
        if (g_dm.drivers[i] == drv) {
            return 0; // Already registered
        }
    }
    
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
    // Skip if already bound
    if (dev->drv) return 0;
    
    for (size_t i = 0; i < g_dm.driver_count; i++) {
        driver_t* drv = g_dm.drivers[i];
        
        if (drv->bus != dev->bus || drv->class_ != dev->class_)
            continue;
        
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
        
        // Skip already initialized devices (from early init)
        if (dev->initialized) {
            matched++;
            continue;
        }
        
        if (device_driver_match_and_bind(dev) == 0) {
            matched++;
            
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
        
        // Skip already initialized devices
        if (dev->initialized) {
            initialized++;
            continue;
        }
        
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