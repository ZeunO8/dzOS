// Builtin driver registration
#include "device_manager.h"
#include "common/printf.h"

// External driver registration functions
extern void register_ps2_driver(void);
extern void register_nvme_driver(void);
extern void register_serial_driver(void);
extern void register_rtc_driver(void);
extern void register_framebuffer_driver(void);

void register_builtin_drivers(void) {
    ktprintf("[DRIVER] Registering builtin drivers...\n");
    
    // Platform drivers (early init)
    register_rtc_driver();
    register_serial_driver();
    register_framebuffer_driver();
    
    // Bus-specific drivers
    register_ps2_driver();
    register_nvme_driver();
    
    ktprintf("[DRIVER] Builtin drivers registered\n");
}