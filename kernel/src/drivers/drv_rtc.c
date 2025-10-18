// RTC Driver - Platform device for real-time clock and TSC calibration
#include "driver.h"
#include "common/printf.h"
#include "cpu/asm.h"
#include "mem/kmalloc.h"

#define PIT_TICK_RATE 1193182ul
#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)
#define RTC_PRECISION 1000000

#define CMOS_ADDRESS_REGISTER 0x70
#define CMOS_DATA_REGISTER 0x71

typedef struct {
    uint64_t tsc_frequency;
    uint64_t initial_rtc;
    uint64_t initial_tsc;
} rtc_device_data_t;

static uint64_t quick_pit_calibrate(void) {
    uint64_t result;
    
    __asm__ volatile(
        "movl $0x61, %%edx\n\t"
        "inb %%dx, %%al\n\t"
        "andb $0xFD, %%al\n\t"
        "orb $0x01, %%al\n\t"
        "outb %%al, %%dx\n\t"
        
        "movl $0x43, %%edx\n\t"
        "movb $0xB0, %%al\n\t"
        "outb %%al, %%dx\n\t"
        
        "movl $0x42, %%edx\n\t"
        "movb $0xFF, %%al\n\t"
        "outb %%al, %%dx\n\t"
        "outb %%al, %%dx\n\t"
        
        "inb %%dx, %%al\n\t"
        "inb %%dx, %%al\n\t"
        
        "xorl %%ecx, %%ecx\n\t"
        "xorq %%r8, %%r8\n\t"
        "xorq %%r9, %%r9\n\t"
        
        "1:\n\t"
        "cmpl $50000, %%ecx\n\t"
        "jge 99f\n\t"
        
        "movl $0x42, %%edx\n\t"
        "inb %%dx, %%al\n\t"
        "inb %%dx, %%al\n\t"
        "cmpb $0xFF, %%al\n\t"
        "jne 2f\n\t"
        
        "movq %%r9, %%r8\n\t"
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "movq %%rax, %%r9\n\t"
        
        "incl %%ecx\n\t"
        "jmp 1b\n\t"
        
        "2:\n\t"
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "subq %%r8, %%rax\n\t"
        "movq %%rax, %%r10\n\t"
        
        "cmpl $5, %%ecx\n\t"
        "jle 99f\n\t"
        
        "movl $1, %%ebx\n\t"
        
        "3:\n\t"
        "cmpl %[max_iter], %%ebx\n\t"
        "jg 99f\n\t"
        
        "movl $0xFF, %%edi\n\t"
        "subl %%ebx, %%edi\n\t"
        
        "xorl %%ecx, %%ecx\n\t"
        "xorq %%r8, %%r8\n\t"
        "xorq %%r11, %%r11\n\t"
        
        "4:\n\t"
        "cmpl $50000, %%ecx\n\t"
        "jge 5f\n\t"
        
        "movl $0x42, %%edx\n\t"
        "inb %%dx, %%al\n\t"
        "inb %%dx, %%al\n\t"
        "cmpb %%dil, %%al\n\t"
        "jne 5f\n\t"
        
        "movq %%r11, %%r8\n\t"
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "movq %%rax, %%r11\n\t"
        
        "incl %%ecx\n\t"
        "jmp 4b\n\t"
        
        "5:\n\t"
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "subq %%r8, %%rax\n\t"
        "movq %%rax, %%r12\n\t"
        
        "cmpl $5, %%ecx\n\t"
        "jle 6f\n\t"
        
        "subq %%r9, %%r11\n\t"
        
        "movl $0xFE, %%edi\n\t"
        "subl %%ebx, %%edi\n\t"
        "movl $0x42, %%edx\n\t"
        "inb %%dx, %%al\n\t"
        "inb %%dx, %%al\n\t"
        "cmpb %%dil, %%al\n\t"
        "jne 6f\n\t"
        
        "movq %%r11, %%rax\n\t"
        "movq %[pit_rate], %%rdx\n\t"
        "mulq %%rdx\n\t"
        
        "movl %%ebx, %%ecx\n\t"
        "shll $8, %%ecx\n\t"
        "divq %%rcx\n\t"
        
        "movq %[min_freq], %%rcx\n\t"
        "cmpq %%rcx, %%rax\n\t"
        "jl 6f\n\t"
        "movq %[max_freq], %%rcx\n\t"
        "cmpq %%rcx, %%rax\n\t"
        "jg 6f\n\t"
        
        "movq %%rax, %0\n\t"
        "jmp 100f\n\t"
        
        "6:\n\t"
        "incl %%ebx\n\t"
        "jmp 3b\n\t"
        
        "99:\n\t"
        "xorq %%rax, %%rax\n\t"
        "movq %%rax, %0\n\t"
        
        "100:\n\t"
        
        : "=r"(result)
        : [max_iter] "i"((int)MAX_QUICK_PIT_ITERATIONS),
          [pit_rate] "i"((uint64_t)PIT_TICK_RATE),
          [min_freq] "i"(500000000ULL),
          [max_freq] "i"(10000000000ULL)
        : "rax", "rbx", "rcx", "rdx", "rdi", "r8", "r9", "r10", "r11", "r12", "cc", "memory"
    );
    
    return result;
}

enum rtc_registers {
    Seconds = 0x00,
    Minutes = 0x02,
    Hours = 0x04,
    DayOfMonth = 0x07,
    Month = 0x08,
    Year = 0x09,
    StatusRegisterA = 0x0A,
    StatusRegisterB = 0x0B
};

static uint8_t read_rtc_register(enum rtc_registers port_number) {
    outb(CMOS_ADDRESS_REGISTER, port_number);
    return inb(CMOS_DATA_REGISTER);
}

static bool is_rtc_updating(void) {
    return read_rtc_register(StatusRegisterA) & 0x80;
}

static uint64_t convert_bcd_to_binary(uint8_t value) {
    return ((value / 16) * 10) + (value & 0x0f);
}

static uint64_t read_rtc_time(void) {
    while (is_rtc_updating());
    
    const uint8_t status_b = read_rtc_register(StatusRegisterB);
    const bool is_binary = status_b & 0x04;
    
    uint8_t seconds, minutes, hours, year, dayofmonth, month;
    uint8_t last_seconds, last_minutes, last_hours, last_year, last_dayofmonth, last_month;
    
    seconds = read_rtc_register(Seconds);
    minutes = read_rtc_register(Minutes);
    hours = read_rtc_register(Hours);
    year = read_rtc_register(Year);
    dayofmonth = read_rtc_register(DayOfMonth);
    month = read_rtc_register(Month);
    
    do {
        last_seconds = seconds;
        last_minutes = minutes;
        last_hours = hours;
        last_year = year;
        last_dayofmonth = dayofmonth;
        last_month = month;
        
        while (is_rtc_updating());
        
        seconds = read_rtc_register(Seconds);
        minutes = read_rtc_register(Minutes);
        hours = read_rtc_register(Hours);
        year = read_rtc_register(Year);
        dayofmonth = read_rtc_register(DayOfMonth);
        month = read_rtc_register(Month);
    } while (last_seconds != seconds || last_minutes != minutes ||
             last_hours != hours || last_year != year ||
             last_dayofmonth != dayofmonth || last_month != month);
    
    if (!is_binary) {
        hours = convert_bcd_to_binary(hours);
        seconds = convert_bcd_to_binary(seconds);
        minutes = convert_bcd_to_binary(minutes);
        year = convert_bcd_to_binary(year);
        month = convert_bcd_to_binary(month);
        dayofmonth = convert_bcd_to_binary(dayofmonth);
    }
    
    const uint64_t daysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint64_t yearsSinceEpoch = (2000 + year) - 1970;
    uint64_t leapYears = yearsSinceEpoch / 4;
    if ((yearsSinceEpoch % 4) > 1) leapYears++;
    
    uint64_t daysCurrentYear = 0;
    for (int i = 0; i < month - 1; i++) {
        daysCurrentYear += daysPerMonth[i];
    }
    daysCurrentYear += dayofmonth;
    
    const uint64_t daysSinceEpoch = (yearsSinceEpoch * 365) - 1;
    const uint64_t unixTimeOfDay = (hours * 3600) + (minutes * 60) + seconds;
    
    return ((daysSinceEpoch * 86400) + (leapYears * 86400) +
            (daysCurrentYear * 86400) + unixTimeOfDay);
}

// Driver operations
static int rtc_probe(device_t *dev) {
    ktprintf("[RTC_DRIVER] Probing RTC/TSC\n");
    return 0;
}

static int rtc_init(device_t *dev) {
    ktprintf("[RTC_DRIVER] Initializing RTC and calibrating TSC\n");
    
    rtc_device_data_t *data = kcmalloc(sizeof(rtc_device_data_t));
    if (!data) return -1;
    
    // Calibrate TSC with retries
    const int max_attempts = 3;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        data->tsc_frequency = quick_pit_calibrate();
        
        if (data->tsc_frequency >= 500000000ULL && data->tsc_frequency <= 10000000000ULL) {
            break;
        }
        
        if (attempt == max_attempts - 1) {
            kmfree(data);
            ktprintf("[RTC_DRIVER] TSC calibration failed\n");
            return -1;
        }
        
        for (volatile int i = 0; i < 100000; i++);
    }
    
    data->initial_rtc = read_rtc_time() * RTC_PRECISION;
    
    __asm__ volatile(
        "xorl %%eax, %%eax\n\t"
        "cpuid\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "movq %%rax, %0"
        : "=r"(data->initial_tsc)
        :
        : "rax", "rbx", "rcx", "rdx", "memory"
    );
    
    dev->driver_data = data;
    
    ktprintf("[RTC_DRIVER] TSC frequency: %llu Hz (%f MHz)\n",
             data->tsc_frequency, data->tsc_frequency / 1e6);
    ktprintf("[RTC_DRIVER] Initial RTC: %llu us\n", data->initial_rtc);
    
    return 0;
}

static int rtc_ioctl(device_t *dev, uint32_t cmd, uintptr_t arg) {
    rtc_device_data_t *data = (rtc_device_data_t *)dev->driver_data;
    if (!data) return -1;
    
    // Commands: 0=get_time_us, 1=get_tsc_freq, 2=delay_ms
    switch (cmd) {
        case 0: { // Get current time in microseconds
            uint64_t tsc_diff = get_tsc() - data->initial_tsc;
            uint64_t us_elapsed = (tsc_diff * RTC_PRECISION) / data->tsc_frequency;
            *(uint64_t *)arg = data->initial_rtc + us_elapsed;
            return 0;
        }
        case 1: { // Get TSC frequency
            *(uint64_t *)arg = data->tsc_frequency;
            return 0;
        }
        case 2: { // Delay milliseconds
            uint64_t ms = (uint64_t)arg;
            uint64_t start = get_tsc();
            double target_ticks = ((double)data->tsc_frequency / 1000.0) * (double)ms;
            while ((double)(get_tsc() - start) < target_ticks)
                __asm__ volatile("pause");
            return 0;
        }
        default:
            return -1;
    }
}

static const driver_ops_t rtc_ops = {
    .probe = rtc_probe,
    .init = rtc_init,
    .read = NULL,
    .write = NULL,
    .remove = NULL,
    .ioctl = rtc_ioctl,
    .irq_handler = NULL
};

static driver_t rtc_driver = {
    .name = "rtc",
    .bus = DRIVER_BUS_PLATFORM,
    .class_ = DRIVER_CLASS_MISC,
    .ops = rtc_ops,
    .priv = NULL,
    .manifest = NULL
};

void register_rtc_driver(void) {
    driver_register_verified(&rtc_driver);
}

// Legacy compatibility
static device_t *g_rtc_dev = NULL;

void rtc_set_global(device_t *dev) {
    g_rtc_dev = dev;
}

void kprint_rtc_init_string(void) {
    if (g_rtc_dev && g_rtc_dev->driver_data) {
        rtc_device_data_t *data = (rtc_device_data_t *)g_rtc_dev->driver_data;
        ktprintf("TSC Frequency set to %llu Hz (%f MHz) and initial RTC is %llu\n",
                 data->tsc_frequency, data->tsc_frequency / 1e6, data->initial_rtc);
    }
}

uint64_t rtc_now(void) {
    if (!g_rtc_dev || !g_rtc_dev->driver_data) return 0;
    uint64_t result;
    driver_ioctl(g_rtc_dev, 0, (uintptr_t)&result);
    return result;
}

void delay_ms(uint64_t ms) {
    if (g_rtc_dev && g_rtc_dev->driver_data) {
        driver_ioctl(g_rtc_dev, 2, (uintptr_t)ms);
    }
}

uint64_t sys_time(void) {
    return rtc_now();
}

double rtc_now_seconds(void) {
    return (double)rtc_now() / (double)RTC_PRECISION;
}

uint64_t rtc_now_us(void) {
    return rtc_now();
}