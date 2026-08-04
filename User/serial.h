/*
 * serial.h — Minimal UART output (no printf dependency)
 *
 * Replaces debug.h/printf. Direct USART register access.
 * Uses USART1 on PA9/PA10.
 */
#pragma once
#include <stdint.h>

void Delay_Init(void);
void Delay_Ms(uint32_t n);
void serial_init(uint32_t baud);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_hex(uint32_t val, int digits);
void serial_dec(uint32_t val);
