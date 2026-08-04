/*
 * USB Descriptors for CH32H417 DFU Bootloader
 *
 * USB 2.0 High-Speed DFU device.
 * VID: 0x0483 (STMicro), PID: 0xDF11 (DFU in DFU mode).
 *
 * CherryUSB string_descriptor_callback expects plain ASCII (const char *).
 * It builds USB string descriptors (bLength, type, UTF-16LE) using strlen().
 * Do NOT return pre-formatted binary descriptors — strlen stops at 0x00.
 *
 * Exception: index 0 (LANGID) — CherryUSB takes 2 raw bytes as the LANGID
 * value in little-endian. 0x0409 (US English) → {0x09, 0x04}.
 */
#include "usbd_core.h"
#include "usbd_dfu.h"
#include "usb_desc.h"

/* ---- Device Descriptor ---- */
__attribute__((aligned(4)))
const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0,
                               0x00,        /* bDeviceClass (use interface)   */
                               0x00,        /* bDeviceSubClass                 */
                               0x00,        /* bDeviceProtocol                 */
                               0x0483,      /* idVendor (STMicroelectronics)   */
                               0xDF11,      /* idProduct (DFU in DFU mode)     */
                               0x0100,      /* bcdDevice                       */
                               0x01)        /* bNumConfigurations              */
};

/* ---- Configuration Descriptor ---- */
__attribute__((aligned(4)))
const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_DESC_LEN,
                               0x01,        /* bNumInterfaces              */
                               0x01,        /* bConfigurationValue         */
                               0x80,        /* bmAttributes (bus-powered)  */
                               100),        /* bMaxPower (200 mA)          */
    DFU_DESCRIPTOR_INIT(STRING_DESC_INTERFACE)
};

__attribute__((aligned(4)))
const uint8_t other_speed_descriptor[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_DESC_LEN,
                                           0x01, 0x01, 0x80, 100),
    DFU_DESCRIPTOR_INIT(STRING_DESC_INTERFACE)
};

/* ---- Device Qualifier Descriptor (for USB 2.0 HS) ---- */
__attribute__((aligned(4)))
const uint8_t qualifier_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0,
                                         0x00, 0x00, 0x00, 0x01)
};

/* ---- String Descriptors (plain ASCII — CherryUSB builds UTF-16LE) ---- */
static const char string_langid[]       = { 0x09, 0x04 };  /* 0x0409 LE */
static const char string_manufacturer[] = "WCH DFU";
static const char string_product[]      = "CH32H417 DFU HS";
static const char string_serial[]       = "00000001";

/* DfuSe interface string for dfu-util sscanf:
 *   "%[^/]/0x%x/%i*%i%c" → name="/0x" ADDR "/" COUNT "*" SIZE UNIT
 * The address in the string is nominal — SET_ADDRESS_POINTER (0x21)
 * overrides it at runtime. */
/* DfuSe format: @name/0xADDR/COUNT*SIZEunitFLAG
 * 'g' = readable+erasable+writeable (all permissions for flash) */
static const char string_interface[] = "@Flash/0x08004000/118*8Kg";

/* ---- CherryUSB Descriptor Callbacks ---- */
const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return qualifier_descriptor;
}

const uint8_t *other_speed_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return other_speed_descriptor;
}

const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    switch (index) {
    case STRING_DESC_LANG:       return string_langid;
    case STRING_DESC_MANU:       return string_manufacturer;
    case STRING_DESC_PROD:       return string_product;
    case STRING_DESC_SERN:       return string_serial;
    case STRING_DESC_INTERFACE:  return string_interface;
    default:                     return NULL;
    }
}
