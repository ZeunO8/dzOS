// rtc.c
#include "rtc.h"
#include "common/printf.h"
#include "cpu/asm.h"

#define PIT_TICK_RATE 1193182ul

/*
 * How many MSB values do we want to see? We aim for
 * a maximum error rate of 500ppm (in practice the
 * real error is much smaller), but refuse to spend
 * more than 50ms on it.
 */
#define MAX_QUICK_PIT_MS 50
#define MAX_QUICK_PIT_ITERATIONS (MAX_QUICK_PIT_MS * PIT_TICK_RATE / 1000 / 256)

/**
 * Pure assembly TSC calibration using PIT.
 * Returns TSC frequency in Hz, or 0 on failure.
 * 
 * This closely mirrors the C version logic.
 */
static uint64_t quick_pit_calibrate(void)
{
    uint64_t result;
    
    __asm__ volatile(
        // Setup PIT - Gate high, disable speaker
        "movl $0x61, %%edx\n\t"
        "inb %%dx, %%al\n\t"
        "andb $0xFD, %%al\n\t"        // Clear bit 1
        "orb $0x01, %%al\n\t"         // Set bit 0
        "outb %%al, %%dx\n\t"
        
        // Counter 2, mode 0, binary count
        "movl $0x43, %%edx\n\t"
        "movb $0xB0, %%al\n\t"
        "outb %%al, %%dx\n\t"
        
        // Start at 0xFFFF
        "movl $0x42, %%edx\n\t"
        "movb $0xFF, %%al\n\t"
        "outb %%al, %%dx\n\t"
        "outb %%al, %%dx\n\t"
        
        // Initial delay - pit_verify_msb(0)
        "inb %%dx, %%al\n\t"          // Read LSB (discard)
        "inb %%dx, %%al\n\t"          // Read MSB (discard)
        
        // ===== pit_expect_msb(0xff, &tsc, &d1) =====
        "xorl %%ecx, %%ecx\n\t"       // count = 0
        "xorq %%r8, %%r8\n\t"         // prev_tsc = 0
        "xorq %%r9, %%r9\n\t"         // tsc = 0
        
        "1:\n\t"
        "cmpl $50000, %%ecx\n\t"
        "jge 99f\n\t"                 // count >= 50000, failed
        
        // pit_verify_msb(0xff)
        "movl $0x42, %%edx\n\t"
        "inb %%dx, %%al\n\t"          // LSB (discard)
        "inb %%dx, %%al\n\t"          // MSB
        "cmpb $0xFF, %%al\n\t"
        "jne 2f\n\t"                  // MSB != 0xFF, break
        
        // Update prev_tsc and tsc
        "movq %%r9, %%r8\n\t"         // prev_tsc = tsc
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "movq %%rax, %%r9\n\t"        // tsc = rdtsc()
        
        "incl %%ecx\n\t"
        "jmp 1b\n\t"
        
        "2:\n\t"
        // Calculate d1
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "subq %%r8, %%rax\n\t"
        "movq %%rax, %%r10\n\t"       // r10 = d1
        
        // Check count > 5
        "cmpl $5, %%ecx\n\t"
        "jle 99f\n\t"                 // Failed
        
        // r9 = tsc (initial TSC value)
        
        // ===== Main loop: for (i = 1; i <= MAX_QUICK_PIT_ITERATIONS; i++) =====
        "movl $1, %%ebx\n\t"          // i = 1
        
        "3:\n\t"
        "cmpl %[max_iter], %%ebx\n\t"
        "jg 99f\n\t"                  // i > MAX_ITER, failed
        
        // ===== pit_expect_msb(0xff - i, &delta, &d2) =====
        "movl $0xFF, %%edi\n\t"
        "subl %%ebx, %%edi\n\t"       // edi = 0xff - i (target MSB)
        
        "xorl %%ecx, %%ecx\n\t"       // count = 0
        "xorq %%r8, %%r8\n\t"         // prev_tsc = 0
        "xorq %%r11, %%r11\n\t"       // delta = 0
        
        "4:\n\t"
        "cmpl $50000, %%ecx\n\t"
        "jge 5f\n\t"                  // count >= 50000, break inner loop
        
        // pit_verify_msb(0xff - i)
        "movl $0x42, %%edx\n\t"
        "inb %%dx, %%al\n\t"          // LSB (discard)
        "inb %%dx, %%al\n\t"          // MSB
        "cmpb %%dil, %%al\n\t"
        "jne 5f\n\t"                  // MSB doesn't match, break
        
        // Update prev_tsc and delta
        "movq %%r11, %%r8\n\t"        // prev_tsc = delta
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "movq %%rax, %%r11\n\t"       // delta = rdtsc()
        
        "incl %%ecx\n\t"
        "jmp 4b\n\t"
        
        "5:\n\t"
        // Calculate d2
        "rdtsc\n\t"
        "shlq $32, %%rdx\n\t"
        "orq %%rdx, %%rax\n\t"
        "subq %%r8, %%rax\n\t"
        "movq %%rax, %%r12\n\t"       // r12 = d2
        
        // Check count > 5
        "cmpl $5, %%ecx\n\t"
        "jle 6f\n\t"                  // Failed this iteration, try next i
        
        // delta -= tsc (r11 -= r9)
        "subq %%r9, %%r11\n\t"        // r11 = delta
        
        // ===== pit_verify_msb(0xfe - i) =====
        "movl $0xFE, %%edi\n\t"
        "subl %%ebx, %%edi\n\t"       // edi = 0xfe - i
        "movl $0x42, %%edx\n\t"
        "inb %%dx, %%al\n\t"          // LSB (discard)
        "inb %%dx, %%al\n\t"          // MSB
        "cmpb %%dil, %%al\n\t"
        "jne 6f\n\t"                  // Verification failed, try next i
        
        // Success! Calculate frequency
        // delta *= PIT_TICK_RATE
        "movq %%r11, %%rax\n\t"
        "movq %[pit_rate], %%rdx\n\t"
        "mulq %%rdx\n\t"              // rdx:rax = delta * PIT_TICK_RATE
        
        // delta /= (i * 256)
        "movl %%ebx, %%ecx\n\t"
        "shll $8, %%ecx\n\t"          // ecx = i * 256
        "divq %%rcx\n\t"              // rax = result
        
        // Sanity check
        "movq %[min_freq], %%rcx\n\t"
        "cmpq %%rcx, %%rax\n\t"
        "jl 6f\n\t"
        "movq %[max_freq], %%rcx\n\t"
        "cmpq %%rcx, %%rax\n\t"
        "jg 6f\n\t"
        
        "movq %%rax, %0\n\t"
        "jmp 100f\n\t"
        
        "6:\n\t"                       // Try next i
        "incl %%ebx\n\t"
        "jmp 3b\n\t"
        
        "99:\n\t"                      // Failed
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

/**
 * Each second, the TSC will increment by this amount.
 */
static uint64_t tsc_frequency;
/**
 * The initial RTC time read from CMOS
 */
static uint64_t initial_rtc;
/**
 * Initial TSC value just after reading the initial epoch
 */
static uint64_t initial_tsc;

uint64_t read_rtc_time();

/**
 * Initializes the RTC component. More especially, it will load the CMOS
 * values to memory and get the frequency of the TSC using the PIT.
 * 
 * Attempts calibration multiple times if it fails.
 */
#define RTC_INIT_STRING_SIZE 255
char rtc_init_string[RTC_INIT_STRING_SIZE] = {0};
void rtc_init(void)
{
    // Try calibration up to 3 times for robustness
    const int max_attempts = 3;
    for (int attempt = 0; attempt < max_attempts; attempt++)
    {
        tsc_frequency = quick_pit_calibrate();
        
        // Sanity check: TSC frequency should be reasonable (500 MHz to 10 GHz)
        if (tsc_frequency >= 500000000ULL && tsc_frequency <= 10000000000ULL)
        {
            break;
        }
        
        if (attempt == max_attempts - 1)
        {
            panic("rtc: TSC calibration failed after multiple attempts\n");
        }
        
        // Small delay before retry
        for (volatile int i = 0; i < 100000; i++)
            ;
    }

    initial_rtc = read_rtc_time() * RTC_PRECISION;

    // Serialize and read TSC
    __asm__ volatile(
        "xorl %%eax, %%eax\n\t"
        "cpuid\n\t"
        "rdtsc\n\t"
        "shl $32, %%rdx\n\t"
        "or %%rdx, %%rax\n\t"
        "movq %%rax, %0"
        : "=r"(initial_tsc)
        :
        : "rax", "rbx", "rcx", "rdx", "memory"
    );

    int rem = RTC_INIT_STRING_SIZE - 1;
    ctprintf(rtc_init_string, &rem, "TSC Frequency set to %llu Hz (%f MHz) and initial RTC is %llu\n",
         tsc_frequency, tsc_frequency / 1e6, initial_rtc);
}

/**
 * Gets the RTC time in unix epoch, with microsecond precision.
 */
uint64_t rtc_now(void)
{
    uint64_t tsc_diff = get_tsc() - initial_tsc;

    uint64_t us_elapsed = (tsc_diff * RTC_PRECISION) / tsc_frequency;

    return initial_rtc + us_elapsed;
}

uint64_t sys_time()
{
    return rtc_now();
}

double rtc_now_seconds(void)
{
    uint64_t tsc_diff = get_tsc() - initial_tsc;
    double elapsed = (double)tsc_diff / (double)tsc_frequency;
    double initial = (double)initial_rtc / (double)RTC_PRECISION;
    return initial + elapsed;
}

uint64_t rtc_now_us(void)
{
    uint64_t tsc_diff = get_tsc() - initial_tsc;
    double elapsed_us = ((double)tsc_diff * 1e6) / (double)tsc_frequency;
    return (uint64_t)((double)initial_rtc * 1e6 / RTC_PRECISION + elapsed_us);
}

void delay_ms(uint64_t ms)
{
    uint64_t start = get_tsc();
    double target_ticks = ((double)tsc_frequency / 1000.0) * (double)ms;
    while ((double)(get_tsc() - start) < target_ticks)
        __asm__ volatile("pause");
}

void kprint_rtc_init_string()
{
    kprintf(rtc_init_string);
}

#define CMOS_ADDRESS_REGISTER 0x70
#define CMOS_DATA_REGISTER 0x71

#define BASE_EPOCH_YEAR 1970
#define BASE_CENTURY 2000

enum rtc_registers
{
    Seconds = 0x00,
    Minutes = 0x02,
    Hours = 0x04,
    WeekDay = 0x06,
    DayOfMonth = 0x07,
    Month = 0x08,
    Year = 0x09,

    StatusRegisterA = 0x0A,
    StatusRegisterB = 0x0B
};

static const uint64_t daysPerMonth[12] = {31, 28, 31, 30, 31, 30,
                                          31, 31, 30, 31, 30, 31};

static uint8_t read_rtc_register(enum rtc_registers port_number)
{
    outb(CMOS_ADDRESS_REGISTER, port_number);
    return inb(CMOS_DATA_REGISTER);
}

static bool is_rtc_updating()
{
    uint8_t statusRegisterA = read_rtc_register(StatusRegisterA);
    return statusRegisterA & 0x80;
}

static uint64_t convert_bcd_to_binary(uint8_t value)
{
    // The formula below converts a hex number into the decimal version (it means:
    // 0x22 will become just 22)
    return ((value / 16) * 10) + (value & 0x0f);
}

uint64_t read_rtc_time()
{
    // Yes is nearly identical to the one in northport! (i implemented it there
    // first and then ported it to Dreamos! :)

    while (is_rtc_updating())
        ;
    const uint8_t rtc_register_statusB = read_rtc_register(StatusRegisterB);
    // Depending on the configuration of the RTC we can have different formats for
    // the time It can be in 12/24 hours format Or it can be in Binary or BCD,
    // binary is the time as it is, BCD use hex values for the time meaning that
    // if read 22:33:12 in BCD it actually is 0x22, 0x33, 0x12, in this case we
    // need to render the numbers in decimal before printing them
    const bool is_24hours = rtc_register_statusB & 0x02;
    const bool is_binary = rtc_register_statusB & 0x04;

    uint8_t seconds = read_rtc_register(Seconds);
    uint8_t minutes = read_rtc_register(Minutes);
    uint8_t hours = read_rtc_register(Hours);
    uint8_t year = read_rtc_register(Year);
    uint8_t dayofmonth = read_rtc_register(DayOfMonth);
    uint8_t month = read_rtc_register(Month);
    uint8_t last_seconds;
    uint8_t last_minutes;
    uint8_t last_hours;
    uint8_t last_year;
    uint8_t last_dayofmonth;
    uint8_t last_month;
    do
    {
        last_seconds = seconds;
        last_minutes = minutes;
        last_hours = hours;
        last_year = year;
        last_dayofmonth = dayofmonth;
        last_month = month;

        while (is_rtc_updating())
            ;
        seconds = read_rtc_register(Seconds);
        minutes = read_rtc_register(Minutes);
        hours = read_rtc_register(Hours);
        year = read_rtc_register(Year);
        dayofmonth = read_rtc_register(DayOfMonth);
        month = read_rtc_register(Month);
    } while (last_seconds != seconds || last_minutes != minutes ||
             last_hours != hours || last_year != year ||
             last_dayofmonth != dayofmonth || last_month != month);

    if (!is_binary)
    {
        hours = convert_bcd_to_binary(hours);
        seconds = convert_bcd_to_binary(seconds);
        minutes = convert_bcd_to_binary(minutes);
        year = convert_bcd_to_binary(year);
        month = convert_bcd_to_binary(month);
        dayofmonth = convert_bcd_to_binary(dayofmonth);
    }

    const uint64_t yearsSinceEpoch =
        (BASE_CENTURY + year) - 1970; // Let's count the number of years passed
                                      // since the Epoch Year: (1970)
    uint64_t leapYears =
        yearsSinceEpoch / 4; // We need to know how many leap years too...

    // if yearsSinceEpoch % 4 is greater/equal than 2 we have to add another leap
    // year
    if ((yearsSinceEpoch % 4) > 1)
    {
        leapYears++;
    }

    uint64_t daysCurrentYear = 0;
    for (int i = 0; i < month - 1; i++)
    {
        daysCurrentYear += daysPerMonth[i];
    }

    daysCurrentYear = daysCurrentYear + (dayofmonth);
    // If the rtc is set using 12 hours, when the hour indicates PM time
    // The 0x80 bit of the hour is set
    if (!is_24hours && (hours & 0x80))
    {
        hours = ((hours & 0x7F) + 12) % 24;
    }

    const uint64_t daysSinceEpoch = (yearsSinceEpoch * 365) - 1;
    const uint64_t unixTimeOfDay = (hours * 3600) + (minutes * 60) + seconds;
    return ((daysSinceEpoch * 86400) + (leapYears * 86400) +
            (daysCurrentYear * 86400) + unixTimeOfDay);
}