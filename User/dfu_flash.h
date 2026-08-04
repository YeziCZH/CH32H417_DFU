#pragma once

#include <stdint.h>

#define DFU_FLASH_APP_BASE   0x08004000u
#define DFU_FLASH_APP_END    0x080F0000u
#define DFU_FLASH_ERASE_SIZE 0x00002000u
#define DFU_FLASH_XFER_SIZE  512u
#define DFU_FLASH_ERASED_WORD 0xE339E339u

enum dfu_flash_error {
    DFU_FLASH_ERR_NONE = 0,
    DFU_FLASH_ERR_ADDRESS,
    DFU_FLASH_ERR_ERASE,
    DFU_FLASH_ERR_WRITE,
    DFU_FLASH_ERR_VERIFY,
    DFU_FLASH_ERR_BUSY,
    DFU_FLASH_ERR_TARGET,
};

void dfu_flash_begin(void);
void dfu_flash_cancel(void);
int dfu_flash_queue_write(uint32_t addr, const uint8_t *data, uint16_t length);
int dfu_flash_queue_erase(uint32_t addr);
void dfu_flash_finish(void);
int dfu_flash_poll(void);
int dfu_flash_busy(void);
int dfu_flash_all_done(void);
enum dfu_flash_error dfu_flash_get_error(void);
void dfu_flash_progress(uint32_t *written, uint32_t *total);
void dfu_flash_fill_poll_timeout(uint8_t buf[3]);
int dfu_flash_read(uint32_t addr, uint8_t *data, uint16_t length,
                   uint16_t *actual_length);
int dfu_flash_address_valid(uint32_t addr, uint32_t length);

extern uint32_t dbg_dfu[6];
