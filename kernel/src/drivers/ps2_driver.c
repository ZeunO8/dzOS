// drivers/ps2_driver.c
#include "drivers/driver.h"
#include "drivers/hw_detect.h"
#include "cpu/asm.h"
#include "device/pic.h"
#include "common/printf.h"

// Import your existing handlers
extern void keyboard_handler(void);
extern void mouse_handler(void);
extern void ps2_init(void); // Your existing initialization

static int ps2_probe(device_t* dev) {
    ps2_device_info_t* info = (ps2_device_info_t*)dev->os_data;
    if (!info || !info->exists) {
        return -1;
    }
    
    ktprintf("[PS2_DRIVER] Probing device on IRQ %d\n", info->irq);
    return 0;
}

static int ps2_init_device(device_t* dev) {
    ps2_device_info_t* info = (ps2_device_info_t*)dev->os_data;
    
    ktprintf("[PS2_DRIVER] Initializing device '%s' (IRQ %d)\n", 
             dev->name, info->irq);
    
    // Call your existing ps2_init which does the full controller setup
    ps2_init();
    
    // Setup interrupts using your existing IOAPIC code
    uint32_t cpu_id = lapic_get_id();
    
    if (info->irq == 1) {
        ioapic_enable(IRQ_KEYBOARD, cpu_id);
    } else if (info->irq == 12) {
        ioapic_enable(IRQ_MOUSE, cpu_id);
    }
    
    return 0;
}

static void ps2_irq_handler(device_t* dev, uint8_t irq) {
    ps2_device_info_t* info = (ps2_device_info_t*)dev->os_data;
    
    if (info->irq == 1) {
        keyboard_handler();
    } else if (info->irq == 12) {
        mouse_handler();
    }
}

static const driver_ops_t ps2_ops = {
    .probe = ps2_probe,
    .init = ps2_init_device,
    .remove = NULL,
    .read = NULL,
    .write = NULL,
    .ioctl = NULL,
    .irq_handler = ps2_irq_handler
};

static const driver_t ps2_driver = {
    .name = "ps2_input",
    .bus = DRIVER_BUS_PS2,
    .class_ = DRIVER_CLASS_INPUT,
    .ops = ps2_ops,
    .priv = NULL,
    .manifest = NULL
};

// Called during register_builtin_drivers()
void ps2_driver_register(void) {
    driver_register_verified((driver_t*)&ps2_driver);
}