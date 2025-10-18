// drivers/builtin_drivers.c
#include "driver.h"
#include "device_manager.h"
#include "common/printf.h"

// Forward declarations of driver registration functions
extern void ps2_driver_register(void);
// extern void vga_driver_register(void);
// extern void serial_driver_register(void);
// Add more as you create them

void register_builtin_drivers(void) {
    ktprintf("[DRIVER] Registering builtin drivers...\n");
    
    // Register all your builtin drivers here
    ps2_driver_register();
    // vga_driver_register();
    // serial_driver_register();
    
    ktprintf("[DRIVER] Builtin drivers registered\n");
}