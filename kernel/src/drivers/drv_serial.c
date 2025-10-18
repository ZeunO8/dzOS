// Serial Port Driver - Character device for COM1
#include "driver.h"
#include "common/printf.h"
#include "common/condvar.h"
#include "cpu/asm.h"
#include "device/pic.h"
#include "mem/kmalloc.h"

#define PORT 0x3f8
#define SERIAL_BUFFER_LENGTH 128
#define IRQ_COM1 4

typedef struct {
    char input_buffer[SERIAL_BUFFER_LENGTH];
    uint8_t read_index;
    uint8_t write_index;
    struct condvar cv;
    bool initialized;
} serial_device_data_t;

static int is_transmit_empty(void) {
    return inb(PORT + 5) & 0b00100000;
}

static int serial_received(void) {
    return inb(PORT + 5) & 0x01;
}

void serial_putc(char a) {
    while (is_transmit_empty() == 0);
    outb(PORT, a);
}

static char serial_getc(void) {
    while (serial_received() == 0);
    return inb(PORT);
}

// Driver operations
static int serial_probe(device_t *dev) {
    ktprintf("[SERIAL_DRIVER] Probing serial port COM1\n");
    
    // Check if serial port exists
    outb(PORT + 1, 0);
    if (inb(PORT + 5) == 0xFF) {
        ktprintf("[SERIAL_DRIVER] No serial port detected\n");
        return -1;
    }
    
    return 0;
}

static int serial_init(device_t *dev) {
    ktprintf("[SERIAL_DRIVER] Initializing COM1 serial port\n");
    
    // Allocate device data
    serial_device_data_t *data = kcmalloc(sizeof(serial_device_data_t));
    if (!data) return -1;
    
    data->read_index = 0;
    data->write_index = 0;
    data->initialized = false;
    
    // Configure serial port
    outb(PORT + 2, 0);              // Turn off FIFO
    outb(PORT + 3, 0b10000000);     // Enable DLAB
    outb(PORT + 0, 115200 / 9600);  // Baudrate low
    outb(PORT + 1, 0);              // Baudrate high
    outb(PORT + 3, 0b00000011);     // Disable DLAB, 8 data bits
    outb(PORT + 4, 0);              // Modem control
    outb(PORT + 1, 0b00000001);     // Enable receive interrupts
    
    // Acknowledge pre-existing interrupts
    inb(PORT + 2);
    inb(PORT + 0);
    
    dev->driver_data = data;
    dev->irq = IRQ_COM1;
    data->initialized = true;
    
    // Enable interrupt at IOAPIC
    ioapic_enable(IRQ_COM1, 0);
    
    ktprintf("[SERIAL_DRIVER] Serial port ready on IRQ %d\n", IRQ_COM1);
    return 0;
}

static int serial_read_op(device_t *dev, void *buffer, size_t len) {
    serial_device_data_t *data = (serial_device_data_t *)dev->driver_data;
    if (!data || len == 0) return 0;
    
    char *buf = (char *)buffer;
    
    condvar_lock(&data->cv);
    
    // Wait for data
    while (data->read_index == data->write_index) {
        condvar_wait(&data->cv);
    }
    
    // Calculate available bytes
    size_t available;
    if (data->write_index < data->read_index) {
        available = data->write_index + (SERIAL_BUFFER_LENGTH - data->read_index);
    } else {
        available = data->write_index - data->read_index;
    }
    
    size_t to_read = len > available ? available : len;
    
    // Copy data
    for (size_t i = 0; i < to_read; i++) {
        buf[i] = data->input_buffer[data->read_index];
        data->read_index = (data->read_index + 1) % SERIAL_BUFFER_LENGTH;
    }
    
    condvar_unlock(&data->cv);
    return to_read;
}

static int serial_write_op(device_t *dev, const void *buffer, size_t len) {
    const char *buf = (const char *)buffer;
    for (size_t i = 0; i < len; i++) {
        serial_putc(buf[i]);
    }
    return len;
}

static void serial_irq_handler(device_t *dev, uint8_t irq) {
    serial_device_data_t *data = (serial_device_data_t *)dev->driver_data;
    if (!data || !data->initialized) {
        lapic_send_eoi();
        return;
    }
    
    char c = serial_getc();
    if (c == '\r') c = '\n';
    
    condvar_lock(&data->cv);
    
    // Add to buffer if space available
    if ((data->write_index + 1) % SERIAL_BUFFER_LENGTH != data->read_index) {
        data->input_buffer[data->write_index] = c;
        data->write_index = (data->write_index + 1) % SERIAL_BUFFER_LENGTH;
        serial_putc(c); // Echo
        condvar_notify_all(&data->cv);
    }
    
    condvar_unlock(&data->cv);
    lapic_send_eoi();
}

static const driver_ops_t serial_ops = {
    .probe = serial_probe,
    .init = serial_init,
    .read = serial_read_op,
    .write = serial_write_op,
    .remove = NULL,
    .ioctl = NULL,
    .irq_handler = serial_irq_handler
};

static driver_t serial_driver = {
    .name = "serial",
    .bus = DRIVER_BUS_PLATFORM,
    .class_ = DRIVER_CLASS_CHAR,
    .ops = serial_ops,
    .priv = NULL,
    .manifest = NULL
};

void register_serial_driver(void) {
    driver_register_verified(&serial_driver);
}

// Legacy compatibility
static device_t *g_serial_dev = NULL;

void serial_set_global(device_t *dev) {
    g_serial_dev = dev;
}

void serial_init_interrupt(void) {
    // Now handled by driver system
}

void serial_received_char(void) {
    if (g_serial_dev && g_serial_dev->drv && g_serial_dev->drv->ops.irq_handler) {
        g_serial_dev->drv->ops.irq_handler(g_serial_dev, IRQ_COM1);
    }
}

int serial_write(const char *buffer, size_t len) {
    if (g_serial_dev && g_serial_dev->drv) {
        return serial_write_op(g_serial_dev, buffer, len);
    }
    // Fallback
    for (size_t i = 0; i < len; i++) {
        serial_putc(buffer[i]);
    }
    return len;
}

int serial_read(char *buffer, size_t len) {
    if (g_serial_dev && g_serial_dev->drv) {
        return serial_read_op(g_serial_dev, buffer, len);
    }
    return 0;
}

int serial_read_async(char *buffer, size_t len) {
    // TODO: Implement async read
    return 0;
}