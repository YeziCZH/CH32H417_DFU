/*
 * CH32H417 DFU Bootloader — V3F core only
 * Minimal printf — uses serial_* directly
 */
#include "ch32h417_conf.h"
#include "system_ch32h417.h"
#include "serial.h"
#include "usbd_core.h"
#include "dfu_flash.h"
#include <string.h>

#define APP_START_ADDR          DFU_FLASH_APP_BASE
#define DFU_MAGIC               0x44465521u
#define DFU_WAIT_MS             500u
#define DFU_TIMEOUT_MS          300000u

#define DFU_BTN_PORT            GPIOA
#define DFU_BTN_PIN             GPIO_Pin_0
#define DFU_BTN_RCC             RCC_HB2Periph_GPIOA

__attribute__((section(".noinit_dfu"))) static uint32_t g_dfu_magic;
static uint8_t g_usb_active;

static void dfu_mode(void);
static void jump_to_app(uint32_t physical_addr);
static int  check_dfu_entry(void);
__attribute__((weak)) void dfu_usb_init(void);
__attribute__((weak)) int  dfu_usb_poll(void);
__attribute__((weak)) uint32_t dfu_usb_get_exec_address(void);

volatile uint32_t g_app_exec_alias;

__attribute__((weak)) void dfu_usb_init(void) { serial_puts("[DFU] USB DFU stub\r\n"); }
__attribute__((weak)) int  dfu_usb_poll(void)  { return 0; }
__attribute__((weak)) uint32_t dfu_usb_get_exec_address(void) { return APP_START_ADDR; }

int main(void) {
    SystemInit(); SystemAndCoreClockUpdate();
    Delay_Init();
    serial_init(115200);

    serial_puts("\r\n========================================\r\n");
    serial_puts("  CH32H417 DFU Bootloader v1.0\r\n");
    serial_puts("  SystemClock: "); serial_dec(SystemClock); serial_puts(" Hz\r\n");
    serial_puts("  CoreClock:   "); serial_dec(SystemCoreClock); serial_puts(" Hz\r\n");
    serial_puts("========================================\r\n\r\n");

    serial_puts("[BOOT] Waiting "); serial_dec(DFU_WAIT_MS); serial_puts("ms for debugger connection...\r\n");
    Delay_Ms(DFU_WAIT_MS);

    if (check_dfu_entry()) {
        serial_puts("[BOOT] DFU entry triggered!\r\n");
        dfu_mode();
    }

    uint32_t app_entry = dfu_usb_get_exec_address();
    if (app_entry < APP_START_ADDR || app_entry >= DFU_FLASH_APP_END ||
        (app_entry & 3u))
        app_entry = APP_START_ADDR;
    serial_puts("[BOOT] Jumping to APP @ 0x");
    serial_hex(app_entry, 8);
    serial_puts("...\r\n\r\n");
    jump_to_app(app_entry);
    while (1) { }
}

static int check_dfu_entry(void) {
    if (g_dfu_magic == DFU_MAGIC) {
        g_dfu_magic = 0;
        serial_puts("[BOOT] DFU entry: SRAM magic word\r\n");
        return 1;
    }
    RCC_HB2PeriphClockCmd(DFU_BTN_RCC, ENABLE);
    { GPIO_InitTypeDef g = {0}; g.GPIO_Pin = DFU_BTN_PIN; g.GPIO_Mode = GPIO_Mode_IPU; GPIO_Init(DFU_BTN_PORT, &g); }
    Delay_Ms(5);
    if (GPIO_ReadInputDataBit(DFU_BTN_PORT, DFU_BTN_PIN) == Bit_SET) {
        serial_puts("[BOOT] DFU entry: GPIO PA0 button\r\n");
        return 1;
    }
    serial_puts("[BOOT] No DFU entry trigger — booting APP\r\n");
    return 0;
}

static void dfu_mode(void) {
    serial_puts("[DFU] Entering DFU mode...\r\n");
    dfu_usb_init();
    g_usb_active = 1;
    serial_puts("[DFU] Waiting for USB host (timeout ");
    serial_dec(DFU_TIMEOUT_MS / 1000); serial_puts("s)...\r\n");

    uint32_t loops = 0;
    const uint32_t max_loops = DFU_TIMEOUT_MS / 1;
    while (loops < max_loops) {
        int s = dfu_usb_poll();
        if (s > 0) {
            serial_puts("[DFU] Leaving DFU mode\r\n");
            return;
        } else if (s < 0) {
            serial_puts("[DFU] Flash error — aborting DFU, booting APP\r\n");
            break;
        }
        Delay_Ms(1);
        loops++;
    }
    serial_puts("[DFU] Timeout — no USB activity, booting APP\r\n");
}

static void jump_to_app(uint32_t physical_addr)
{
    uint32_t first_word = *(volatile uint32_t *)physical_addr;

    if (first_word == DFU_FLASH_ERASED_WORD || first_word == 0u) {
        serial_puts("[BOOT] APP entry is blank; staying in bootloader\r\n");
        while (1) {}
    }

    /* CH32H417 programs flash through 0x08000000 but executes it through
     * the zero-based alias, exactly as the WCH EVT IAP example does. */
    g_app_exec_alias = physical_addr - 0x08000000u;
    Delay_Ms(20);
    if (g_usb_active)
        usb_dc_deinit(0);
    SysTick0->CTLR = 0;
    GPIO_DeInit(GPIOA);
    GPIO_DeInit(GPIOB);
    USART_DeInit(USART1);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOB |
                          RCC_HB2Periph_USART1, DISABLE);
    RCC_DeInit();

    NVIC_DisableIRQ(USBHS_IRQn);
    NVIC_EnableIRQ(Software_IRQn);
    NVIC_SetPendingIRQ(Software_IRQn);
    while (1) {}
}

void dfu_request_reboot(void) {
    g_dfu_magic = DFU_MAGIC;
    __disable_irq();
    NVIC_SystemReset();
    while (1) { }
}
