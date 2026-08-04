/*
 * serial.c — Minimal UART output + SysTick delay via USART1 (PA9/PA10)
 */
#include "ch32h417_conf.h"
#include "system_ch32h417.h"
#include "serial.h"

/* ── SysTick delay (from debug.c, V3F core) ─────────────────────────────── */
static uint32_t p_us, p_ms;
static uint8_t tx_stalled;

void Delay_Init(void) {
    p_us = HCLKClock / 1000000;
    p_ms = (uint16_t)p_us * 1000;
}

void Delay_Ms(uint32_t n) {
    SysTick0->ISR &= ~(1 << 0);
    uint32_t i = (uint32_t)n * p_ms;
    SysTick0->CNT = 0;
    SysTick0->CMP = i;
    SysTick0->CTLR = (1 << 2);
    SysTick0->CTLR |= (1 << 0);
    while ((SysTick0->ISR & (1 << 0)) != (1 << 0)) {}
    SysTick0->CTLR &= ~(1 << 0);
}

/* ── UART output ────────────────────────────────────────────────────────── */

void serial_init(uint32_t baud) {
    GPIO_InitTypeDef g = {0};
    USART_InitTypeDef u = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_USART1 | RCC_HB2Periph_AFIO, ENABLE);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF7);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF7);

    g.GPIO_Pin   = GPIO_Pin_9;
    g.GPIO_Speed = GPIO_Speed_Very_High;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &g);

    g.GPIO_Pin  = GPIO_Pin_10;
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &g);

    u.USART_BaudRate            = baud;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &u);
    USART_Cmd(USART1, ENABLE);
    tx_stalled = 0;
}

int serial_getc_nonblock(void) {
    if (USART1->STATR & USART_STATR_RXNE)
        return (int)(USART1->DATAR & 0xFFu);
    return -1;
}

void serial_putc(char c) {
    uint32_t timeout = 1000000u;

    /* Debug output must never be able to stop the DFU state machine. */
    if (tx_stalled) {
        if (!(USART1->STATR & USART_STATR_TC))
            return;
        tx_stalled = 0;
    }
    while (!(USART1->STATR & USART_STATR_TC)) {
        if (--timeout == 0) {
            tx_stalled = 1;
            return;
        }
    }
    USART1->DATAR = (uint8_t)c;
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

void serial_hex(uint32_t val, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--)
        serial_putc(hex[(val >> (i * 4)) & 0xF]);
}

void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i) serial_putc(buf[--i]);
}
