// drivers/drv_ps2.c
#include "drivers/driver.h"
#include "drivers/hw_detect.h"
#include "cpu/asm.h"
#include "device/pic.h"
#include "common/printf.h"
#include "common/term.h"
#include "cpu/idt.h"

uint8_t scancode_set = 0;

// Forward declarations for internal functions
static void ps2_controller_init(void);
static void detect_scancode_set(void);
static void mouse_write(uint8_t data);
static uint8_t mouse_read(void);

void keyboard_handler(void);
void mouse_handler(void);

// ===== PS/2 Utility Functions =====

static inline void ps2_wait_read(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(PS2_STATUS_PORT) & 0x01)
            return;
    }
}

static inline void ps2_wait_write(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (!(inb(PS2_STATUS_PORT) & 0x02))
            return;
    }
}

// ===== Driver Operations =====

static int ps2_probe(device_t* dev) {
    ps2_device_info_t *info = (ps2_device_info_t *)dev->os_data;
    ktprintf("[PS2_DRIVER] Probing device on IRQ %d\n", info->irq);
    return 0;
}

extern void isr_stub_33(void); // Keyboard
extern void isr_stub_44(void); // Mouse

static int ps2_init(device_t* dev) {
    ps2_device_info_t *info = (ps2_device_info_t *)dev->os_data;
    ktprintf("[PS2_DRIVER] Initializing device '%s' (IRQ %d)\n", 
             dev->name, info->irq);
    
    // Only initialize the controller once (on first device)
    static bool controller_initialized = false;
    if (!controller_initialized) {
        ps2_controller_init();
        controller_initialized = true;
    }
    
    // Enable the IRQ for this specific device
    ioapic_enable(info->irq, 0);

    if (info->irq == 1)
        idt_set_gate(T_IRQ0 + IRQ_KEYBOARD, (uint64_t)isr_stub_33, 0, 0x8E);
    else if (info->irq == 12)
        idt_set_gate(T_IRQ0 + IRQ_MOUSE,    (uint64_t)isr_stub_44, 0, 0x8E);
    
    ktprintf("[PS/2] Controller initialized.\n");
    ktprintf("[PS/2] Scancode Set = %i\n", scancode_set);
    
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
    .init = ps2_init,
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

void register_ps2_driver(void) {
    driver_register_verified((driver_t*)&ps2_driver);
}

// ===== Keyboard Handler =====

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool capslock_on = false;
static bool release_next = false;

static void emit_utf8(uint32_t codepoint) {
    char buf[4];
    int len = 0;
    if (codepoint <= 0x7F) {
        buf[0] = codepoint;
        len = 1;
    } else if (codepoint <= 0x7FF) {
        buf[0] = 0xC0 | ((codepoint >> 6) & 0x1F);
        buf[1] = 0x80 | (codepoint & 0x3F);
        len = 2;
    } else if (codepoint <= 0xFFFF) {
        buf[0] = 0xE0 | ((codepoint >> 12) & 0x0F);
        buf[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        buf[2] = 0x80 | (codepoint & 0x3F);
        len = 3;
    } else {
        buf[0] = 0xF0 | ((codepoint >> 18) & 0x07);
        buf[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        buf[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        buf[3] = 0x80 | (codepoint & 0x3F);
        len = 4;
    }
    for (int i = 0; i < len; i++)
        term_putc(buf[i]);
}

static const uint32_t us_layout[128][4] = {
    [0x02] = {'1','!',0,0}, [0x03] = {'2','@',0,0}, [0x04] = {'3','#',0,0},
    [0x05] = {'4','$',0,0}, [0x06] = {'5','%',0,0}, [0x07] = {'6','^',0,0},
    [0x08] = {'7','&',0,0}, [0x09] = {'8','*',0,0}, [0x0A] = {'9','(',0,0},
    [0x0B] = {'0',')',0,0}, [0x0C] = {'-','_',0,0}, [0x0D] = {'=','+',0,0},
    [0x0E] = {'\b','\b',0,0}, [0x0F] = {'\t','\t',0,0},
    [0x10] = {'q','Q',0,0}, [0x11] = {'w','W',0,0}, [0x12] = {'e','E',0,0},
    [0x13] = {'r','R',0,0}, [0x14] = {'t','T',0,0}, [0x15] = {'y','Y',0,0},
    [0x16] = {'u','U',0,0}, [0x17] = {'i','I',0,0}, [0x18] = {'o','O',0,0},
    [0x19] = {'p','P',0,0}, [0x1A] = {'[','{',0,0}, [0x1B] = {']','}',0,0},
    [0x1C] = {'\n','\n',0,0}, [0x1D] = {0,0,0,0}, [0x1E] = {'a','A',0,0},
    [0x1F] = {'s','S',0,0}, [0x20] = {'d','D',0,0}, [0x21] = {'f','F',0,0},
    [0x22] = {'g','G',0,0}, [0x23] = {'h','H',0,0}, [0x24] = {'j','J',0,0},
    [0x25] = {'k','K',0,0}, [0x26] = {'l','L',0,0}, [0x27] = {';',':',0,0},
    [0x28] = {'\'','"',0,0}, [0x29] = {'`','~',0,0}, [0x2B] = {'\\','|',0,0},
    [0x2C] = {'z','Z',0,0}, [0x2D] = {'x','X',0,0}, [0x2E] = {'c','C',0,0},
    [0x2F] = {'v','V',0,0}, [0x30] = {'b','B',0,0}, [0x31] = {'n','N',0,0},
    [0x32] = {'m','M',0,0}, [0x33] = {',','<',0,0}, [0x34] = {'.','>',0,0},
    [0x35] = {'/','?',0,0}, [0x39] = {' ',' ',0,0},
};
static const uint32_t (*layout_map)[128][4] = &us_layout;

static const uint8_t us_set2_to_set1[128] = {
    [0x01]=0x3B,[0x02]=0x00,[0x03]=0x3F,[0x04]=0x3D,[0x05]=0x3B,[0x06]=0x3C,
    [0x07]=0x58,[0x08]=0x00,[0x09]=0x44,[0x0A]=0x42,[0x0B]=0x40,[0x0C]=0x3E,
    [0x0D]=0x0F,[0x0E]=0x29,[0x0F]=0x00,[0x10]=0x00,[0x11]=0x38,[0x12]=0x2A,
    [0x13]=0x00,[0x14]=0x1D,[0x15]=0x10,[0x16]=0x02,[0x17]=0x00,[0x18]=0x00,
    [0x19]=0x00,[0x1A]=0x2C,[0x1B]=0x1F,[0x1C]=0x1E,[0x1D]=0x11,[0x1E]=0x03,
    [0x1F]=0x00,[0x20]=0x00,[0x21]=0x2E,[0x22]=0x2D,[0x23]=0x20,[0x24]=0x12,
    [0x25]=0x05,[0x26]=0x04,[0x27]=0x00,[0x28]=0x00,[0x29]=0x39,[0x2A]=0x2F,
    [0x2B]=0x21,[0x2C]=0x14,[0x2D]=0x13,[0x2E]=0x06,[0x2F]=0x00,[0x30]=0x00,
    [0x31]=0x31,[0x32]=0x30,[0x33]=0x23,[0x34]=0x22,[0x35]=0x15,[0x36]=0x07,
    [0x37]=0x00,[0x38]=0x00,[0x39]=0x00,[0x3A]=0x32,[0x3B]=0x24,[0x3C]=0x16,
    [0x3D]=0x08,[0x3E]=0x09,[0x3F]=0x00,[0x40]=0x00,[0x41]=0x33,[0x42]=0x25,
    [0x43]=0x17,[0x44]=0x18,[0x45]=0x0B,[0x46]=0x0A,[0x47]=0x00,[0x48]=0x00,
    [0x49]=0x34,[0x4A]=0x35,[0x4B]=0x26,[0x4C]=0x27,[0x4D]=0x19,[0x4E]=0x0C,
    [0x4F]=0x00,[0x50]=0x00,[0x51]=0x00,[0x52]=0x28,[0x53]=0x00,[0x54]=0x1A,
    [0x55]=0x0D,[0x56]=0x00,[0x57]=0x00,[0x58]=0x3A,[0x59]=0x36,[0x5A]=0x1C,
    [0x5B]=0x1B,[0x5C]=0x00,[0x5D]=0x2B,[0x5E]=0x00,[0x5F]=0x00,[0x60]=0x00,
    [0x61]=0x00,[0x62]=0x00,[0x63]=0x00,[0x64]=0x00,[0x65]=0x00,[0x66]=0x0E,
    [0x67]=0x00,[0x68]=0x00,[0x69]=0x4F,[0x6A]=0x00,[0x6B]=0x4B,[0x6C]=0x47,
    [0x6D]=0x00,[0x6E]=0x00,[0x6F]=0x00,[0x70]=0x52,[0x71]=0x53,[0x72]=0x50,
    [0x73]=0x4C,[0x74]=0x4E,[0x75]=0x48,[0x76]=0x01,[0x77]=0x45,[0x78]=0x57,
    [0x79]=0x4E,[0x7A]=0x4D,[0x7B]=0x4A,[0x7C]=0x37,[0x7D]=0x49,[0x7E]=0x46,
    [0x7F]=0x00
};

void keyboard_handler(void) {
    if (!(inb(PS2_STATUS_PORT) & 0x01)) {
        lapic_send_eoi();
        return;
    }

    uint8_t scancode = inb(PS2_DATA_PORT);
    lapic_send_eoi();

    bool key_released = false;
    if (scancode_set == 2) {
        if (scancode == 0xF0) {
            release_next = true;
            return;
        }
        if (release_next) {
            key_released = true;
            release_next = false;
            scancode = us_set2_to_set1[scancode];
        } else if (scancode & 0x80) {
            key_released = true;
            scancode &= 0x7F;
            scancode = us_set2_to_set1[scancode];
        } else {
            scancode = us_set2_to_set1[scancode];
        }
    } else if (scancode_set == 1) {
        key_released = scancode & 0x80;
        scancode &= 0x7F;
    }

    // Track modifiers
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = !key_released;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = !key_released;
        return;
    }
    if (scancode == 0x38) {
        alt_pressed = !key_released;
        return;
    }
    if (scancode == 0x3A && !key_released) {
        capslock_on = !capslock_on;
        return;
    }

    if (key_released) return;

    uint32_t codepoint = 0;
    const uint32_t (*map)[4] = *layout_map;

    if (shift_pressed ^ capslock_on)
        codepoint = map[scancode][1];
    else
        codepoint = map[scancode][0];

    if (codepoint)
        emit_utf8(codepoint);
}

// ===== Mouse Handler =====

static uint8_t mouse_cycle = 0;
static int8_t mouse_packet[3];

static void mouse_write(uint8_t data) {
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xD4);
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
}

static uint8_t mouse_read(void) {
    ps2_wait_read();
    return inb(PS2_DATA_PORT);
}

void mouse_handler(void) {
    uint8_t status_before = inb(PS2_STATUS_PORT);
    if (!(status_before & 0x01)) {
        lapic_send_eoi();
        return;
    }

    uint8_t data = inb(PS2_DATA_PORT);
    mouse_packet[mouse_cycle++] = data;

    // Synchronize first byte
    if (mouse_cycle == 1 && !(mouse_packet[0] & 0x08)) {
        mouse_cycle = 0;
        lapic_send_eoi();
        return;
    }

    if (mouse_cycle == 3) {
        // uint8_t flags = mouse_packet[0];
        // int8_t dx = mouse_packet[1];
        // int8_t dy = mouse_packet[2];
        // ktprintf("[MOUSE] dx=%d dy=%d flags=0x%x\n", dx, dy, flags);
        mouse_cycle = 0;
    }

    lapic_send_eoi();
}

// ===== PS/2 Controller Initialization =====

static void detect_scancode_set(void) {
    ps2_wait_write();
    outb(PS2_DATA_PORT, 0xF0);

    ps2_wait_read();
    inb(PS2_DATA_PORT);

    ps2_wait_write();
    outb(PS2_DATA_PORT, 0x00);

    ps2_wait_read();
    inb(PS2_DATA_PORT);

    ps2_wait_read();
    scancode_set = inb(PS2_DATA_PORT);
}

static void ps2_controller_init(void) {
    // Disable both PS/2 ports
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAD);
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xA7);

    // Flush buffers
    while (inb(PS2_STATUS_PORT) & 0x01)
        inb(PS2_DATA_PORT);

    // Read config byte
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA_PORT);

    // Enable interrupts on both ports, disable translation
    config |= 0x03;
    config &= ~0x40;

    // Write back config
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0x60);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    // Enable both ports
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xAE);
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, 0xA8);

    // Mouse reset sequence
    mouse_write(0xFF);
    mouse_read();
    mouse_read();
    mouse_read();
    mouse_write(0xF4);
    mouse_read(); // Enable data reporting

    detect_scancode_set();
}