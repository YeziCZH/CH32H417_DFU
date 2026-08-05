/*
 * CH32H417 DFU Bootloader — V3F core only
 * Minimal printf — uses serial_* directly
 */
#include "ch32h417_conf.h"
#include "system_ch32h417.h"
#include "serial.h"
#include "usbd_core.h"
#include "usbd_dfu.h"
#include "dfu_flash.h"
#include "dfu_boot.h"
#include <string.h>

#define APP_START_ADDR          DFU_FLASH_APP_BASE
#define DFU_WAIT_MS             500u
#define DFU_DEFAULT_INACTIVITY_MS   2000u
#define DFU_TRIGGER_INACTIVITY_MS   30000u

#define DFU_UART_PORT           GPIOC
#define DFU_UART_TX_PIN         GPIO_Pin_6
#define DFU_UART_RX_PIN         GPIO_Pin_7
#define DFU_UART_RCC            RCC_HB2Periph_GPIOC

__attribute__((section(".dfu_magic"))) static volatile uint32_t g_dfu_magic;
static uint8_t g_usb_active;

static void dfu_mode(uint32_t inactivity_timeout_ms);
static void jump_to_app(uint32_t physical_addr);
static int  wait_for_dfu_entry(void);
static int  check_dfu_entry(void);
static int  debug_uart_key_pressed(void);
static int  app_entry_valid(uint32_t physical_addr);
static void dfu_entry_gpio_init(void);
__attribute__((weak)) void dfu_usb_init(void);
__attribute__((weak)) int  dfu_usb_poll(void);
__attribute__((weak)) uint32_t dfu_usb_get_exec_address(void);
__attribute__((weak)) int  dfu_usb_timeout_can_boot(void);

volatile uint32_t g_app_exec_alias;

__attribute__((weak)) void dfu_usb_init(void) { serial_puts("[DFU] USB DFU stub\r\n"); }
__attribute__((weak)) int  dfu_usb_poll(void)  { return 0; }
__attribute__((weak)) uint32_t dfu_usb_get_exec_address(void) { return APP_START_ADDR; }
__attribute__((weak)) int  dfu_usb_timeout_can_boot(void) { return 1; }

int main(void) {
    SystemInit(); SystemAndCoreClockUpdate();
    Delay_Init();
    dfu_entry_gpio_init();
    serial_init(115200);

    serial_puts("\r\n[BOOT] CH32H417 DFU\r\n");

    {
        int triggered = wait_for_dfu_entry();
        if (triggered || !app_entry_valid(APP_START_ADDR)) {
            if (!triggered)
                serial_puts("[BOOT] Default APP is invalid; staying in DFU\r\n");
            dfu_mode(triggered ? DFU_TRIGGER_INACTIVITY_MS :
                       DFU_DEFAULT_INACTIVITY_MS);
        }
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

static int wait_for_dfu_entry(void)
{
    uint32_t waited;

    for (waited = 0; waited < DFU_WAIT_MS; waited++) {
        if (check_dfu_entry())
            return 1;
        if (debug_uart_key_pressed()) {
            serial_puts("[BOOT] DFU entry: debug UART key\r\n");
            return 1;
        }
        Delay_Ms(1);
    }
    if (check_dfu_entry())
        return 1;
    serial_puts("[BOOT] No DFU entry trigger; booting APP\r\n");
    return 0;
}

static int check_dfu_entry(void) {
    if (g_dfu_magic == DFU_BOOT_MAGIC_VALUE) {
        g_dfu_magic = 0;
        serial_puts("[BOOT] DFU entry: SRAM magic word\r\n");
        return 1;
    }
    if (GPIO_ReadInputDataBit(DFU_UART_PORT, DFU_UART_RX_PIN) == Bit_SET) {
        serial_puts("[BOOT] DFU entry: UART4 RX high\r\n");
        return 1;
    }
    return 0;
}

static int debug_uart_key_pressed(void)
{
    int ch;

    while ((ch = serial_getc_nonblock()) >= 0) {
        if (ch == '\r' || ch == '\n' || ch == ' ')
            return 1;
    }
    return 0;
}

static void dfu_entry_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_HB2PeriphClockCmd(DFU_UART_RCC, ENABLE);
    /* TX must come up high. RX is pulled down so an open input cannot
     * request DFU; a PC6-PC7 short or an external controller can drive it. */
    GPIO_SetBits(DFU_UART_PORT, DFU_UART_TX_PIN);
    gpio.GPIO_Pin = DFU_UART_TX_PIN;
    gpio.GPIO_Speed = GPIO_Speed_Low;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(DFU_UART_PORT, &gpio);
    gpio.GPIO_Pin = DFU_UART_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(DFU_UART_PORT, &gpio);
    Delay_Ms(5);
}

static void dfu_mode(uint32_t inactivity_timeout_ms) {
    serial_puts("[DFU] Entering DFU mode...\r\n");
    dfu_usb_init();
    g_usb_active = 1;
    serial_puts("[DFU] Protocol inactivity timeout: ");
    serial_dec(inactivity_timeout_ms / 1000); serial_puts("s\r\n");

    uint32_t inactive_ms = 0;
    uint32_t activity = usbd_dfu_get_activity_count();
    while (1) {
        int s = dfu_usb_poll();
        if (s > 0) {
            serial_puts("[DFU] Leaving DFU mode\r\n");
            return;
        } else if (s < 0) {
            /* Keep reporting the DFU error until the host clears it. */
            inactive_ms = 0;
            Delay_Ms(1);
            continue;
        }
        Delay_Ms(1);
        if (activity != usbd_dfu_get_activity_count()) {
            activity = usbd_dfu_get_activity_count();
            inactive_ms = 0;
        } else if (++inactive_ms >= inactivity_timeout_ms) {
            /* Timeout cancels an incomplete session. A custom execute address
             * is committed only by a successful manifestation. */
            usbd_dfu_force_idle();
            if (dfu_usb_timeout_can_boot() && app_entry_valid(APP_START_ADDR)) {
                serial_puts("[DFU] Inactivity timeout; booting APP\r\n");
                return;
            }
            serial_puts("[DFU] Staying in DFU\r\n");
            inactive_ms = 0;
        }
    }
}

static int app_entry_valid(uint32_t physical_addr)
{
    uint32_t first_word;

    if (physical_addr < APP_START_ADDR || physical_addr >= DFU_FLASH_APP_END ||
        (physical_addr & 3u))
        return 0;
    first_word = *(volatile uint32_t *)physical_addr;
    return first_word != DFU_FLASH_ERASED_WORD && first_word != 0u &&
           first_word != 0xFFFFFFFFu;
}

static void jump_to_app(uint32_t physical_addr)
{
    if (!app_entry_valid(physical_addr)) {
        serial_puts("[BOOT] APP entry is invalid; entering DFU\r\n");
        dfu_mode(DFU_DEFAULT_INACTIVITY_MS);
        physical_addr = dfu_usb_get_exec_address();
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
    GPIO_DeInit(GPIOC);
    USART_DeInit(USART1);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOB |
                          RCC_HB2Periph_GPIOC |
                          RCC_HB2Periph_USART1, DISABLE);
    RCC_DeInit();

    NVIC_DisableIRQ(USBHS_IRQn);
    NVIC_EnableIRQ(Software_IRQn);
    NVIC_SetPendingIRQ(Software_IRQn);
    while (1) {}
}

void dfu_request_reboot(void) {
    g_dfu_magic = DFU_BOOT_MAGIC_VALUE;
    __asm volatile("fence rw, rw" ::: "memory");
    __disable_irq();
    NVIC_SystemReset();
    while (1) { }
}
