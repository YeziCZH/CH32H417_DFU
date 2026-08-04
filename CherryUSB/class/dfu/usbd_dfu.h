/*
 * Copyright (c) 2022 ~ 2026, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef USBD_DFU_H
#define USBD_DFU_H

#include "usb_dfu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Init dfu interface driver */
struct usbd_interface *usbd_dfu_init_intf(struct usbd_interface *intf);
uint8_t usbd_dfu_get_state(void);
void usbd_dfu_force_idle(void);
uint32_t usbd_dfu_get_activity_count(void);
void usbd_dfu_record_activity(void);

void usbd_dfu_begin_load(void);
void usbd_dfu_end_load(void);
void usbd_dfu_begin_upload(void);
void usbd_dfu_end_upload(void);
void usbd_dfu_abort(void);
void usbd_dfu_reset(void);
int usbd_dfu_write(uint16_t value, const uint8_t *data, uint16_t length);
int usbd_dfu_read(uint16_t value, uint8_t *data, uint16_t length, uint16_t *actual_length);
int usbd_dfu_flash_done(void);
uint8_t usbd_dfu_get_status(void);
void usbd_dfu_fill_poll_timeout(uint8_t buf[3]);
int usbd_dfu_set_address(uint32_t addr);
int usbd_dfu_erase_cmd(uint32_t addr);
int usbd_dfu_set_exec_address(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* USBD_DFU_H */
