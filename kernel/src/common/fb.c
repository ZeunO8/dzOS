#include "fb.h"

struct limine_framebuffer *framebuffer = 0;

__attribute__((used, section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

int init_framebuffer()
{
    if (framebuffer_request.response == 0 || framebuffer_request.response->framebuffer_count < 1)
    {
        return 1;
    }

    framebuffer = framebuffer_request.response->framebuffers[0];
    return 0;
}

struct limine_framebuffer *get_framebuffer()
{
    return framebuffer;
}