#include "term.h"
#include "limine.h"
#include "fb.h"

#include "flanterm.h"
#include "flanterm_backends/fb.h"

struct flanterm_context *ft_ctx = 0;

int init_term()
{
    struct limine_framebuffer *framebuffer = get_framebuffer();

    uint32_t width = framebuffer->width;
    uint32_t height = framebuffer->height;
    uint32_t pitch = framebuffer->pitch;
    uint32_t *framebuffer_ptr = (uint32_t*)framebuffer->address;

    ft_ctx = flanterm_fb_init(
        0,
        0,
        framebuffer_ptr, width, height, pitch,
        framebuffer->red_mask_size, framebuffer->red_mask_shift,
        framebuffer->green_mask_size, framebuffer->green_mask_shift,
        framebuffer->blue_mask_size, framebuffer->blue_mask_shift,
        0,
        0, 0,
        0, 0,
        0, 0,
        0, 0, 0, 1,
        0, 0,
        0
    );

    return 0;
}

void term_putc(char c)
{
    char s[2] = {c, 0};
    flanterm_write(ft_ctx, s, 1);
}