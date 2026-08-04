/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CherryUSB Configuration for CH32H417 DFU Project
 */
#ifndef USB_CONFIG_H
#define USB_CONFIG_H

/*!< USB print speed - disabled */
#define CONFIG_USB_PRINTF(...)

/*!< log level: 0=off, 1=error, 2=warn, 3=info, 4=debug */
#define CONFIG_USB_DBG_LEVEL 0U

/*!< Enable print color */
#define CONFIG_USB_PRINTF_COLOR_ENABLE 0U

/*!< data align size, use 4 for CH32 */
#define CONFIG_USB_ALIGN_SIZE 4U

/*!< Enable USB Device, max bus count */
#define CONFIG_USBDEV_MAX_BUS 1

/*!< USB HS (High Speed) support */
#define CONFIG_USB_HS

/*!< Device descriptor request buffer length */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512

/*!< CH32H417 has NO cache, define empty */
#define USB_NOCACHE_RAM_SECTION

/*!< EP0 max transfer size (USB 2.0 HS = 64) */
#define CONFIG_USBDEV_EP0_MAX_TRANSFER_SIZE 64

#endif /* USB_CONFIG_H */
