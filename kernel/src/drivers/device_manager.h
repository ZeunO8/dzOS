#pragma once
#include "driver.h"
#include "hw_detect.h"

#define MAX_DEVICES 256

struct limine_framebuffer; // Forward declaration

typedef struct {
    device_t devices[MAX_DEVICES];
    size_t device_count;
    driver_t* drivers[MAX_DEVICES];
    size_t driver_count;
} device_manager_t;

void device_manager_init(void);

// Called by hardware discovery
int device_register_from_pci(const pci_device_info_t* hw_info);
int device_register_from_ps2(const ps2_device_info_t* hw_info);
int device_register_platform(const char* name, driver_class_t class_);
int device_register_framebuffer(struct limine_framebuffer *fb);

// Device lookup
device_t* device_find_by_name(const char* name);

// Driver registration
int driver_register_verified(driver_t* drv);
int driver_unregister(driver_t* drv);

// Matching and initialization
int device_driver_match_and_bind(device_t* dev);
void device_manager_probe_all(void);
void device_manager_init_all(void);

device_t* device_find_by_irq(uint8_t irq);
