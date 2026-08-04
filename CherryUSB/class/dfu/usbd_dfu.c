/*
 * Copyright (c) 2022 ~ 2026, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbd_core.h"
#include "usbd_dfu.h"

struct usbd_dfu_priv {
    uint8_t state;
    uint8_t status;
    bool download_active;
    bool upload_active;
};

static struct usbd_dfu_priv g_dfu = {
    .state = DFU_STATE_DFU_IDLE,
    .status = DFU_STATUS_OK,
};

const char *usbd_dfu_state_string[] = {
    "APP_IDLE", "APP_DETACH", "DFU_IDLE", "DFU_DNLOAD_SYNC",
    "DFU_DNLOAD_BUSY", "DFU_DNLOAD_IDLE", "DFU_MANIFEST_SYNC",
    "DFU_MANIFEST", "DFU_MANIFEST_WAIT_RESET", "DFU_UPLOAD_IDLE",
    "DFU_ERROR"
};

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void dfu_set_error(uint8_t fallback)
{
    uint8_t status = usbd_dfu_get_status();
    g_dfu.status = (status == DFU_STATUS_OK) ? fallback : status;
    g_dfu.state = DFU_STATE_DFU_ERROR;
}

static void dfu_cancel(void)
{
    usbd_dfu_abort();
    g_dfu.download_active = false;
    g_dfu.upload_active = false;
}

static void dfu_fill_status(uint8_t *data, uint32_t *len)
{
    uint8_t media_status = usbd_dfu_get_status();

    if (media_status != DFU_STATUS_OK && g_dfu.state != DFU_STATE_DFU_ERROR) {
        g_dfu.status = media_status;
        g_dfu.state = DFU_STATE_DFU_ERROR;
    }

    if (g_dfu.state != DFU_STATE_DFU_ERROR) {
        switch (g_dfu.state) {
        case DFU_STATE_DFU_DNLOAD_SYNC:
        case DFU_STATE_DFU_DNLOAD_BUSY:
            g_dfu.state = usbd_dfu_flash_done() ?
                          DFU_STATE_DFU_DNLOAD_IDLE : DFU_STATE_DFU_DNLOAD_BUSY;
            break;
        case DFU_STATE_DFU_MANIFEST_SYNC:
            g_dfu.state = DFU_STATE_DFU_MANIFEST;
            break;
        case DFU_STATE_DFU_MANIFEST:
            if (usbd_dfu_flash_done())
                g_dfu.state = DFU_STATE_DFU_MANIFEST_WAIT_RESET;
            break;
        default:
            break;
        }
    }

    data[0] = g_dfu.status;
    if (g_dfu.state == DFU_STATE_DFU_DNLOAD_BUSY ||
        g_dfu.state == DFU_STATE_DFU_MANIFEST) {
        usbd_dfu_fill_poll_timeout(&data[1]);
    } else {
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
    }
    data[4] = g_dfu.state;
    data[5] = 0;
    *len = 6;
}

/* DfuSe commands are valid only in download block zero. This is important:
 * data blocks 0x21 and 0x41 must never be mistaken for commands. */
static bool dfu_handle_command(struct usb_setup_packet *setup, uint8_t *data)
{
    uint8_t cmd;
    uint32_t addr;
    int ret;

    if (setup->wValue != 0 || setup->wLength != 5 || data == NULL)
        return false;

    cmd = data[0];
    addr = get_le32(&data[1]);
    if (cmd != DFU_SPECIAL_CMD_SET_ADDRESS_POINTER &&
        cmd != DFU_SPECIAL_CMD_ERASE &&
        cmd != DFU_SPECIAL_CMD_SET_EXEC_ADDRESS)
        return false;

    if (cmd == DFU_SPECIAL_CMD_SET_ADDRESS_POINTER)
        ret = usbd_dfu_set_address(addr);
    else if (cmd == DFU_SPECIAL_CMD_ERASE)
        ret = usbd_dfu_erase_cmd(addr);
    else
        ret = usbd_dfu_set_exec_address(addr);

    if (ret < 0)
        dfu_set_error(DFU_STATUS_ERR_ADDRESS);
    else
        g_dfu.state = DFU_STATE_DFU_DNLOAD_SYNC;
    return true;
}

static int dfu_dnload(struct usb_setup_packet *setup, uint8_t *data)
{
    if (setup->wLength == 0) {
        usbd_dfu_end_load();
        g_dfu.download_active = false;
        g_dfu.state = DFU_STATE_DFU_MANIFEST_SYNC;
        return 0;
    }

    if (dfu_handle_command(setup, data))
        return 0;

    if (!g_dfu.download_active) {
        usbd_dfu_begin_load();
        g_dfu.download_active = true;
    }
    if (usbd_dfu_write(setup->wValue, data, setup->wLength) < 0) {
        dfu_set_error(DFU_STATUS_ERR_WRITE);
        return 0;
    }
    g_dfu.state = DFU_STATE_DFU_DNLOAD_SYNC;
    return 0;
}

static int dfu_upload(struct usb_setup_packet *setup, uint8_t *data, uint32_t *len)
{
    uint16_t actual = 0;

    if (!g_dfu.upload_active) {
        usbd_dfu_begin_upload();
        g_dfu.upload_active = true;
    }
    if (usbd_dfu_read(setup->wValue, data, setup->wLength, &actual) < 0) {
        dfu_set_error(DFU_STATUS_ERR_ADDRESS);
        return 0;
    }

    *len = actual;
    if (actual < setup->wLength) {
        usbd_dfu_end_upload();
        g_dfu.upload_active = false;
        g_dfu.state = DFU_STATE_DFU_IDLE;
    } else {
        g_dfu.state = DFU_STATE_DFU_UPLOAD_IDLE;
    }
    return 0;
}

static int dfu_class_interface_request_handler(uint8_t busid,
                                               struct usb_setup_packet *setup,
                                               uint8_t **data, uint32_t *len)
{
    (void)busid;

    if (setup->bRequest == DFU_REQUEST_GETSTATE) {
        (*data)[0] = g_dfu.state;
        *len = 1;
        return 0;
    }
    if (setup->bRequest == DFU_REQUEST_GETSTATUS) {
        dfu_fill_status(*data, len);
        return 0;
    }

    if (g_dfu.state == DFU_STATE_DFU_ERROR) {
        if (setup->bRequest == DFU_REQUEST_CLRSTATUS) {
            dfu_cancel();
            g_dfu.status = DFU_STATUS_OK;
            g_dfu.state = DFU_STATE_DFU_IDLE;
            return 0;
        }
        return -1;
    }

    switch (g_dfu.state) {
    case DFU_STATE_DFU_IDLE:
        if (setup->bRequest == DFU_REQUEST_DNLOAD)
            return dfu_dnload(setup, *data);
        if (setup->bRequest == DFU_REQUEST_UPLOAD)
            return dfu_upload(setup, *data, len);
        if (setup->bRequest == DFU_REQUEST_ABORT) {
            dfu_cancel();
            return 0;
        }
        break;

    case DFU_STATE_DFU_DNLOAD_IDLE:
        if (setup->bRequest == DFU_REQUEST_DNLOAD)
            return dfu_dnload(setup, *data);
        /* DfuSe upload starts immediately after a block-0 SET_ADDRESS
         * command, which leaves the interface in dfuDNLOAD-IDLE. */
        if (setup->bRequest == DFU_REQUEST_UPLOAD && !g_dfu.download_active)
            return dfu_upload(setup, *data, len);
        if (setup->bRequest == DFU_REQUEST_ABORT) {
            dfu_cancel();
            g_dfu.state = DFU_STATE_DFU_IDLE;
            return 0;
        }
        break;

    case DFU_STATE_DFU_UPLOAD_IDLE:
        if (setup->bRequest == DFU_REQUEST_UPLOAD)
            return dfu_upload(setup, *data, len);
        if (setup->bRequest == DFU_REQUEST_ABORT) {
            dfu_cancel();
            g_dfu.state = DFU_STATE_DFU_IDLE;
            return 0;
        }
        break;

    case DFU_STATE_DFU_DNLOAD_SYNC:
    case DFU_STATE_DFU_DNLOAD_BUSY:
    case DFU_STATE_DFU_MANIFEST_SYNC:
    case DFU_STATE_DFU_MANIFEST:
    case DFU_STATE_DFU_MANIFEST_WAIT_RESET:
        break;

    default:
        break;
    }

    return -1;
}

static void dfu_notify_handler(uint8_t busid, uint8_t event, void *arg)
{
    (void)busid;
    (void)arg;
    if (event == USBD_EVENT_RESET) {
        dfu_cancel();
        g_dfu.state = DFU_STATE_DFU_IDLE;
        g_dfu.status = DFU_STATUS_OK;
    }
}

struct usbd_interface *usbd_dfu_init_intf(struct usbd_interface *intf)
{
    intf->class_interface_handler = dfu_class_interface_request_handler;
    intf->class_endpoint_handler = NULL;
    intf->vendor_handler = NULL;
    intf->notify_handler = dfu_notify_handler;
    return intf;
}

uint8_t usbd_dfu_get_state(void)
{
    return g_dfu.state;
}

void usbd_dfu_force_idle(void)
{
    dfu_cancel();
    g_dfu.status = DFU_STATUS_OK;
    g_dfu.state = DFU_STATE_DFU_IDLE;
}

__WEAK void usbd_dfu_begin_load(void) {}
__WEAK void usbd_dfu_end_load(void) {}
__WEAK void usbd_dfu_begin_upload(void) {}
__WEAK void usbd_dfu_end_upload(void) {}
__WEAK void usbd_dfu_abort(void) {}
__WEAK void usbd_dfu_reset(void) {}

__WEAK int usbd_dfu_write(uint16_t value, const uint8_t *data, uint16_t length)
{
    (void)value; (void)data; (void)length;
    return 0;
}

__WEAK int usbd_dfu_read(uint16_t value, uint8_t *data, uint16_t length,
                         uint16_t *actual_length)
{
    (void)value; (void)data; (void)length;
    *actual_length = 0;
    return 0;
}

__WEAK int usbd_dfu_flash_done(void) { return 1; }
__WEAK uint8_t usbd_dfu_get_status(void) { return DFU_STATUS_OK; }

__WEAK void usbd_dfu_fill_poll_timeout(uint8_t buf[3])
{
    buf[0] = 1; buf[1] = 0; buf[2] = 0;
}

__WEAK int usbd_dfu_set_address(uint32_t addr) { (void)addr; return -1; }
__WEAK int usbd_dfu_erase_cmd(uint32_t addr) { (void)addr; return -1; }
__WEAK int usbd_dfu_set_exec_address(uint32_t addr) { (void)addr; return -1; }
