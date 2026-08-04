#include "ch32h417_conf.h"
#include "system_ch32h417.h"
#include "serial.h"
#include "dfu_boot.h"

#ifndef JUMP_APP_ALIAS
#define JUMP_APP_ALIAS 0x00004000u
#endif

int main(void)
{
    SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();
    serial_init(115200);

    serial_puts("\r\n[JUMP-APP] PASS alias=0x");
    serial_hex(JUMP_APP_ALIAS, 8);
    serial_puts("\r\n");
    Delay_Ms(500);
#if JUMP_APP_HANG
    serial_puts("[JUMP-APP] Simulated hang\r\n");
    while (1) {}
#endif
#if JUMP_APP_ENTER_DFU
    *(volatile uint32_t *)DFU_BOOT_MAGIC_ADDRESS = DFU_BOOT_MAGIC_VALUE;
    __asm volatile("fence rw, rw" ::: "memory");
    serial_puts("[JUMP-APP] Request DFU by magic\r\n");
#endif
    NVIC_SystemReset();
    while (1) {}
}
