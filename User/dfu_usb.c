/* CH32H417 USB DFU media binding. */
#include "ch32h417_conf.h"
#include "system_ch32h417.h"
#include "serial.h"
#include "usbd_core.h"
#include "usbd_dfu.h"
#include "usb_desc.h"
#include "dfu_flash.h"

#ifndef DFU_DEBUG_STAY_IN_DFU
#define DFU_DEBUG_STAY_IN_DFU 1
#endif

static uint32_t g_address_pointer = DFU_FLASH_APP_BASE;
static uint32_t g_exec_address = DFU_FLASH_APP_BASE;
static uint8_t g_dfuse_addressing;
static uint8_t g_manifest_reported;
static uint8_t g_error_reported;
static uint8_t g_wait_reset_ms;

static void reset_addressing(void)
{
    g_address_pointer = DFU_FLASH_APP_BASE;
    g_exec_address = DFU_FLASH_APP_BASE;
    g_dfuse_addressing = 0;
}

static int selected_app_blank(void)
{
    uint32_t first_word;

    __asm volatile("fence" ::: "memory");
    first_word = *(volatile uint32_t *)g_exec_address;
    return first_word == DFU_FLASH_ERASED_WORD || first_word == 0u;
}

static void resume_dfu_idle(void)
{
    usbd_dfu_force_idle();
    dfu_flash_begin();
    reset_addressing();
    g_manifest_reported = 0;
    g_error_reported = 0;
    g_wait_reset_ms = 0;
}

static int block_address(uint16_t block, uint32_t *addr)
{
    uint32_t base;
    uint32_t index;
    uint32_t offset;

    if (g_dfuse_addressing) {
        if (block < 2)
            return -1;
        base = g_address_pointer;
        index = (uint32_t)block - 2u;
    } else {
        base = DFU_FLASH_APP_BASE;
        index = block;
    }
    if (index > UINT32_MAX / DFU_FLASH_XFER_SIZE)
        return -1;
    offset = index * DFU_FLASH_XFER_SIZE;
    if (base > UINT32_MAX - offset)
        return -1;
    *addr = base + offset;
    return 0;
}

static void usb_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    if (event == USBD_EVENT_CONFIGURED)
        serial_puts("[USB] Configured\r\n");
}

void usbd_dfu_begin_load(void)
{
    dfu_flash_begin();
    g_manifest_reported = 0;
    g_error_reported = 0;
    g_wait_reset_ms = 0;
}

void usbd_dfu_end_load(void)
{
    dfu_flash_finish();
}

void usbd_dfu_begin_upload(void)
{
    /* Upload is read-only and has no flash job lifecycle. */
}

void usbd_dfu_end_upload(void)
{
    reset_addressing();
}

void usbd_dfu_abort(void)
{
    dfu_flash_cancel();
    reset_addressing();
    g_manifest_reported = 0;
    g_error_reported = 0;
    g_wait_reset_ms = 0;
}

void usbd_dfu_reset(void)
{
}

int usbd_dfu_set_address(uint32_t addr)
{
    if (!dfu_flash_address_valid(addr, 1))
        return -1;
    g_address_pointer = addr;
    g_dfuse_addressing = 1;
    serial_puts("[DFUSE] Address 0x");
    serial_hex(addr, 8);
    serial_puts("\r\n");
    return 0;
}

int usbd_dfu_erase_cmd(uint32_t addr)
{
    serial_puts("[DFUSE] Erase 0x");
    serial_hex(addr, 8);
    serial_puts("\r\n");
    return dfu_flash_queue_erase(addr);
}

int usbd_dfu_set_exec_address(uint32_t addr)
{
    if (!dfu_flash_address_valid(addr, 1) || (addr & 3u))
        return -1;
    g_exec_address = addr;
    serial_puts("[DFU] Execute 0x");
    serial_hex(addr, 8);
    serial_puts("\r\n");
    return 0;
}

uint32_t dfu_usb_get_exec_address(void)
{
    return g_exec_address;
}

int usbd_dfu_write(uint16_t block, const uint8_t *data, uint16_t length)
{
    uint32_t addr;
    if (block_address(block, &addr) < 0)
        return -1;
    return dfu_flash_queue_write(addr, data, length);
}

int usbd_dfu_read(uint16_t block, uint8_t *data, uint16_t length,
                  uint16_t *actual_length)
{
    uint32_t addr;
    if (block_address(block, &addr) < 0)
        return -1;
    return dfu_flash_read(addr, data, length, actual_length);
}

int usbd_dfu_flash_done(void)
{
    return !dfu_flash_busy();
}

uint8_t usbd_dfu_get_status(void)
{
    switch (dfu_flash_get_error()) {
    case DFU_FLASH_ERR_NONE:    return DFU_STATUS_OK;
    case DFU_FLASH_ERR_ADDRESS: return DFU_STATUS_ERR_ADDRESS;
    case DFU_FLASH_ERR_ERASE:   return DFU_STATUS_ERR_ERASE;
    case DFU_FLASH_ERR_WRITE:   return DFU_STATUS_ERR_WRITE;
    case DFU_FLASH_ERR_VERIFY:  return DFU_STATUS_ERR_VERIFY;
    case DFU_FLASH_ERR_BUSY:    return DFU_STATUS_ERR_NOTDONE;
    case DFU_FLASH_ERR_TARGET:  return DFU_STATUS_ERR_TARGET;
    default:                    return DFU_STATUS_ERR_UNKNOWN;
    }
}

void usbd_dfu_fill_poll_timeout(uint8_t buf[3])
{
    dfu_flash_fill_poll_timeout(buf);
}

void dfu_usb_init(void)
{
    static struct usb_descriptor desc = {
        .device_descriptor_callback = device_descriptor_callback,
        .config_descriptor_callback = config_descriptor_callback,
        .device_quality_descriptor_callback = device_quality_descriptor_callback,
        .other_speed_descriptor_callback = other_speed_descriptor_callback,
        .string_descriptor_callback = string_descriptor_callback,
    };
    static struct usbd_interface intf;

    dfu_flash_begin();
    reset_addressing();
    usbd_desc_register(0, &desc);
    usbd_dfu_init_intf(&intf);
    usbd_add_interface(0, &intf);
    usbd_initialize(0, (uintptr_t)USBHSD_BASE, usb_event_handler);
    serial_puts("[DFU] USBHS DFU ready\r\n");
}

int dfu_usb_poll(void)
{
    int ret = dfu_flash_poll();
    if (ret < 0) {
        if (!g_error_reported) {
            serial_puts("[DFU] Flash media error; reporting through GETSTATUS\r\n");
            g_error_reported = 1;
        }
        return 0;
    }

    if (dfu_flash_all_done() &&
        usbd_dfu_get_state() == DFU_STATE_DFU_MANIFEST_WAIT_RESET) {
        if (!g_manifest_reported) {
            uint32_t written, total;
            dfu_flash_progress(&written, &total);
            serial_puts("[DFU] Manifest complete: ");
            serial_dec(written);
            serial_puts("/");
            serial_dec(total);
            serial_puts(" bytes\r\n");
            g_manifest_reported = 1;
        }
        if (g_wait_reset_ms < 50) {
            g_wait_reset_ms++;
            return 0;
        }
        if (selected_app_blank()) {
            serial_puts("[DFU] Selected APP is blank; staying in DFU\r\n");
            resume_dfu_idle();
            return 0;
        }
#if DFU_DEBUG_STAY_IN_DFU
        serial_puts("[DFU] Debug stay in DFU after manifest\r\n");
        resume_dfu_idle();
        return 0;
#else
        return 1;
#endif
    }
    g_wait_reset_ms = 0;
    return 0;
}
