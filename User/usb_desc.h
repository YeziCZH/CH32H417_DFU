/*
 * USB Descriptors for CH32H417 DFU Bootloader
 *
 * Single configuration with one DFU interface.
 * Uses WCH VID: 0x1A86 and custom PID: 0xDF00 (DFU bootloader).
 */
#ifndef __USB_DESC_H
#define __USB_DESC_H

#include "ch32h417.h"

/* Descriptor lengths */
#define USB_DEVICE_DESC_LEN   18
/* 9(cfg) + 9(if) + 9(DFU func) = 27 */
#define USB_CONFIG_DESC_LEN   27
#define USB_QUALIFY_DESC_LEN  10

/* String indices */
#define STRING_DESC_LANG       0
#define STRING_DESC_MANU       1
#define STRING_DESC_PROD       2
#define STRING_DESC_SERN       3
#define STRING_DESC_INTERFACE  5  /* DfuSe interface layout string */

/* CherryUSB descriptor callbacks */
const uint8_t *device_descriptor_callback(uint8_t speed);
const uint8_t *config_descriptor_callback(uint8_t speed);
const uint8_t *device_quality_descriptor_callback(uint8_t speed);
const uint8_t *other_speed_descriptor_callback(uint8_t speed);
const char    *string_descriptor_callback(uint8_t speed, uint8_t index);

#endif /* __USB_DESC_H */
