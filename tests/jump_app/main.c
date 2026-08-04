#include "ch32h417_conf.h"
#include "system_ch32h417.h"
#include "serial.h"

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
    NVIC_SystemReset();
    while (1) {}
}
