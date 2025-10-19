// rtc.h
#pragma once
#include <stdint.h>

/**
 * The precision of the RTC driver is milliseconds
 */
#define RTC_PRECISION 1000000

void rtc_init(void);
uint64_t rtc_now(void);
double rtc_now_seconds(void);
uint64_t rtc_now_us(void);
void delay_ms(uint64_t ms);