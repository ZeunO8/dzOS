#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

enum output_mode {
    OUTPUT_SERIAL,
    OUTPUT_FLANTERM,
    OUTPUT_FRAMEBUFFER
};

void set_output_mode(enum output_mode mode);

void kputc(char c);
int cputc(char* dest, int* rem, char c);
void kprintint(long long xx, int base, int sign);
void kprintptr(uint64_t x);
void kprintfloat(double f, int precision);
void kprints(char* s);
int cprintint(char* dest, int* rem, long long xx, int base, int sign);
int cprintptr(char* dest, int* rem, uint64_t x);
int cprintfloat(char* dest, int* rem, double f, int precision);
int kvprintf(const char *fmt, va_list ap);
int cvprintf(char* dest, int* rem, const char *fmt, va_list ap);
int kprintf(const char *fmt, ...);
int cprintf(char* dest, int* rem, const char *fmt, ...);
int snprintf(char* dest, int dest_len, const char *fmt, ...);
int ktprintf(const char *fmt, ...);
int ctprintf(char* dest, int* rem, const char *fmt, ...);

void khexdump(const char *buf, size_t size);
int chexdump(char* dest, int* rem, const char *buf, size_t size);
void panic(const char *s) __attribute__ ((noreturn));
//|**|
// === Reset ===
#define COLOR_RESET        "\033[0m"

// === Standard Foreground Colors ===
#define COLOR_BLACK_FG     "\033[30m"
#define COLOR_RED_FG       "\033[31m"
#define COLOR_GREEN_FG     "\033[32m"
#define COLOR_YELLOW_FG    "\033[33m"
#define COLOR_BLUE_FG      "\033[34m"
#define COLOR_MAGENTA_FG   "\033[35m"
#define COLOR_CYAN_FG      "\033[36m"
#define COLOR_WHITE_FG     "\033[37m"

// === Bright Foreground Colors ===
#define COLOR_BRIGHT_BLACK_FG   "\033[90m"
#define COLOR_BRIGHT_RED_FG     "\033[91m"
#define COLOR_BRIGHT_GREEN_FG   "\033[92m"
#define COLOR_BRIGHT_YELLOW_FG  "\033[93m"
#define COLOR_BRIGHT_BLUE_FG    "\033[94m"
#define COLOR_BRIGHT_MAGENTA_FG "\033[95m"
#define COLOR_BRIGHT_CYAN_FG    "\033[96m"
#define COLOR_BRIGHT_WHITE_FG   "\033[97m"

// === Standard Background Colors ===
#define COLOR_BLACK_BG     "\033[40m"
#define COLOR_RED_BG       "\033[41m"
#define COLOR_GREEN_BG     "\033[42m"
#define COLOR_YELLOW_BG    "\033[43m"
#define COLOR_BLUE_BG      "\033[44m"
#define COLOR_MAGENTA_BG   "\033[45m"
#define COLOR_CYAN_BG      "\033[46m"
#define COLOR_WHITE_BG     "\033[47m"

// === Bright Background Colors ===
#define COLOR_BRIGHT_BLACK_BG   "\033[100m"
#define COLOR_BRIGHT_RED_BG     "\033[101m"
#define COLOR_BRIGHT_GREEN_BG   "\033[102m"
#define COLOR_BRIGHT_YELLOW_BG  "\033[103m"
#define COLOR_BRIGHT_BLUE_BG    "\033[104m"
#define COLOR_BRIGHT_MAGENTA_BG "\033[105m"
#define COLOR_BRIGHT_CYAN_BG    "\033[106m"
#define COLOR_BRIGHT_WHITE_BG   "\033[107m"

// === Style Macros (Optional) ===
#define STYLE_BOLD        "\033[1m"
#define STYLE_FAINT       "\033[2m"
#define STYLE_ITALIC      "\033[3m"
#define STYLE_UNDERLINE   "\033[4m"
#define STYLE_BLINK       "\033[5m"
#define STYLE_REVERSE     "\033[7m"
#define STYLE_HIDDEN      "\033[8m"
#define STYLE_STRIKE      "\033[9m"

// === Reset variants ===
#define STYLE_RESET_BOLD        "\033[21m"
#define STYLE_RESET_FAINT       "\033[22m"
#define STYLE_RESET_ITALIC      "\033[23m"
#define STYLE_RESET_UNDERLINE   "\033[24m"
#define STYLE_RESET_BLINK       "\033[25m"
#define STYLE_RESET_REVERSE     "\033[27m"
#define STYLE_RESET_HIDDEN      "\033[28m"
#define STYLE_RESET_STRIKE      "\033[29m"