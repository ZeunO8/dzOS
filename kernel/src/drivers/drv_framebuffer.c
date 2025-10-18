// Framebuffer Driver - Display device for linear framebuffer
#include "driver.h"
#include "common/lib.h"
#include "common/printf.h"
#include "limine.h"
#include "mem/kmalloc.h"

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} __attribute__((packed)) framebuffer_pixel_t;

typedef struct {
    struct limine_framebuffer *fb;
    uint64_t current_width;
    uint64_t current_height;
} fb_device_data_t;

// Framebuffer control commands
#define FB_CTL_GET_WIDTH 0
#define FB_CTL_GET_MAX_WIDTH 1
#define FB_CTL_SET_WIDTH 2
#define FB_CTL_GET_HEIGHT 3
#define FB_CTL_GET_MAX_HEIGHT 4
#define FB_CTL_SET_HEIGHT 5
#define FB_CTL_CLEAR 6

static int fb_probe(device_t *dev) {
    ktprintf("[FB_DRIVER] Probing framebuffer device\n");
    return 0;
}

static int fb_init(device_t *dev) {
    // Framebuffer info must be passed in os_data
    struct limine_framebuffer *limine_fb = (struct limine_framebuffer *)dev->os_data;
    if (!limine_fb) {
        ktprintf("[FB_DRIVER] No framebuffer info provided\n");
        return -1;
    }
    
    fb_device_data_t *data = (fb_device_data_t *)kcmalloc(sizeof(fb_device_data_t));
    if (!data) return -1;
    
    data->fb = limine_fb;
    data->current_width = limine_fb->width;
    data->current_height = limine_fb->height;
    
    dev->driver_data = data;
    dev->name = "fb0";
    
    ktprintf("[FB_DRIVER] Framebuffer initialized: %llux%llu @ %u bpp\n",
             data->current_width, data->current_height, (uint32_t)limine_fb->bpp);
    
    if (limine_fb->bpp != 32) {
        ktprintf("[FB_DRIVER] Warning: Expected 32 bpp, got %u\n", (uint32_t)limine_fb->bpp);
    }
    
    return 0;
}

static int fb_write_op(device_t *dev, const void *buffer, size_t size) {
    fb_device_data_t *data = (fb_device_data_t *)dev->driver_data;
    if (!data || !data->fb) return -1;
    
    struct limine_framebuffer *fb = data->fb;
    framebuffer_pixel_t *fb_ptr = (framebuffer_pixel_t *)fb->address;
    const framebuffer_pixel_t *given_buffer = (const framebuffer_pixel_t *)buffer;
    
    size_t row = 0, displayed = 0;
    
    // Copy each row
    while (size > data->current_width && row < data->current_height) {
        memcpy(&fb_ptr[row * (fb->pitch / 4)], given_buffer, data->current_width * 4);
        row++;
        displayed += data->current_width;
        given_buffer += data->current_width;
        size -= data->current_width;
    }
    
    // Copy final remaining row
    if (row < data->current_height && size != 0) {
        memcpy(&fb_ptr[row * (fb->pitch / 4)], given_buffer, size * 4);
        displayed += size;
    }
    
    return displayed;
}

static int fb_ioctl_op(device_t *dev, uint32_t cmd, uintptr_t arg) {
    fb_device_data_t *data = (fb_device_data_t *)dev->driver_data;
    if (!data || !data->fb) return -1;
    
    struct limine_framebuffer *fb = data->fb;
    
    switch (cmd) {
        case FB_CTL_GET_WIDTH:
            *(uint64_t *)arg = data->current_width;
            return 0;
            
        case FB_CTL_GET_MAX_WIDTH:
            *(uint64_t *)arg = fb->width;
            return 0;
            
        case FB_CTL_SET_WIDTH:
            if ((uint64_t)arg <= fb->width) {
                data->current_width = (uint64_t)arg;
                return 0;
            }
            return -3; // Invalid width
            
        case FB_CTL_GET_HEIGHT:
            *(uint64_t *)arg = data->current_height;
            return 0;
            
        case FB_CTL_GET_MAX_HEIGHT:
            *(uint64_t *)arg = fb->height;
            return 0;
            
        case FB_CTL_SET_HEIGHT:
            if ((uint64_t)arg <= fb->height) {
                data->current_height = (uint64_t)arg;
                return 0;
            }
            return -3; // Invalid height
            
        case FB_CTL_CLEAR:
            memset(fb->address, 0, fb->width * fb->height * fb->bpp / 8);
            return 0;
            
        default:
            return -2; // Unknown command
    }
}

static const driver_ops_t fb_ops = {
    .probe = fb_probe,
    .init = fb_init,
    .read = NULL,
    .write = fb_write_op,
    .remove = NULL,
    .ioctl = fb_ioctl_op,
    .irq_handler = NULL
};

static driver_t fb_driver = {
    .name = "framebuffer",
    .bus = DRIVER_BUS_PLATFORM,
    .class_ = DRIVER_CLASS_DISPLAY,
    .ops = fb_ops,
    .priv = NULL,
    .manifest = NULL
};

void register_framebuffer_driver(void) {
    driver_register_verified(&fb_driver);
}

// Legacy compatibility
static device_t *g_fb_dev = NULL;

void fb_set_global(device_t *dev) {
    g_fb_dev = dev;
}

// void fb_init(struct limine_framebuffer *fb) {
//     // Now handled by driver system
//     // This is called early to register the device
// }

int fb_write(const char *buffer, size_t size) {
    if (g_fb_dev && g_fb_dev->drv) {
        return fb_write_op(g_fb_dev, buffer, size);
    }
    return -1;
}

int fb_control(int command, void *data) {
    if (g_fb_dev && g_fb_dev->drv) {
        return driver_ioctl(g_fb_dev, command, (uintptr_t)data);
    }
    return -1;
}