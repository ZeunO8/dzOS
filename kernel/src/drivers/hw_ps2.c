// hw_ps2.c
#include "hw_detect.h"
#include "device_manager.h"
#include "cpu/asm.h"
#include "common/printf.h"

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

static inline void ps2_wait_write(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (!(inb(PS2_STATUS_PORT) & 0x02))
            return;
    }
}

static inline void ps2_wait_read(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(PS2_STATUS_PORT) & 0x01)
            return;
    }
}

int hw_detect_ps2_scan(void) {
    ktprintf("[HW_DETECT] Scanning PS/2 devices...\n");
    
    // Disable both PS/2 ports first
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAD); // Disable first port
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xA7); // Disable second port
    
    // Flush output buffer
    while (inb(PS2_STATUS_PORT) & 0x01)
        inb(PS2_DATA_PORT);
    
    // Read configuration byte
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA_PORT);
    
    // Check if dual channel controller
    bool dual_channel = (config & 0x20) != 0;
    
    // Perform controller self-test
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAA);
    ps2_wait_read();
    uint8_t result = inb(PS2_DATA_PORT);
    
    if (result != 0x55) {
        ktprintf("[HW_DETECT] PS/2 controller self-test failed (0x%x)\n", result);
        return 0;
    }
    
    // Re-write configuration (self-test may reset it)
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x60);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);
    
    int found = 0;
    
    // Test first port (keyboard)
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAB);
    ps2_wait_read();
    result = inb(PS2_DATA_PORT);
    
    if (result == 0x00) {
        ktprintf("[HW_DETECT] Found PS/2 keyboard on port 1\n");
        ps2_device_info_t kb_info = {
            .port = 0x60,
            .irq = 1,
            .exists = true
        };
        device_register_from_ps2(&kb_info);
        found++;
    }
    
    // Test second port (mouse) if dual channel
    if (dual_channel) {
        ps2_wait_write();
        outb(PS2_COMMAND_PORT, 0xA9);
        ps2_wait_read();
        result = inb(PS2_DATA_PORT);
        
        if (result == 0x00) {
            ktprintf("[HW_DETECT] Found PS/2 mouse on port 2\n");
            ps2_device_info_t mouse_info = {
                .port = 0x60,
                .irq = 12,
                .exists = true
            };
            device_register_from_ps2(&mouse_info);
            found++;
        }
    }
    
    ktprintf("[HW_DETECT] Found %d PS/2 devices\n", found);
    return found;
}