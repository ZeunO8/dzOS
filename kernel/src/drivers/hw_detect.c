// hw_detect.c - Updated to skip already-registered devices
#include "hw_detect.h"
#include "device_manager.h"
#include "common/printf.h"

void hw_detect_init(void) {
    ktprintf("[HW_DETECT] Initialized hardware detection subsystem\n");
}

int hw_detect_platform_devices(void) {
    ktprintf("[HW_DETECT] Scanning platform devices...\n");
    
    int found = 0;

    // Register RTC as a platform device (may already be registered by early init)
    if (device_register_platform("rtc", DRIVER_CLASS_MISC) == 0) {
        // Only print if we actually registered it (not already present)
        if (!device_find_by_name("rtc")->initialized) {
            ktprintf("[HW_DETECT] Found RTC (platform device)\n");
        }
        found++;
    }
    
    // Register serial port as a platform device
    if (device_register_platform("serial", DRIVER_CLASS_CHAR) == 0) {
        ktprintf("[HW_DETECT] Found serial port COM1 (platform device)\n");
        found++;
    }
    
    ktprintf("[HW_DETECT] Found %d platform devices\n", found);
    return found;
}