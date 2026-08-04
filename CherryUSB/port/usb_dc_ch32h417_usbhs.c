/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CH32H417 USBHS (USB 2.0 High-Speed) Device Controller Driver
 *
 * Implements the CherryUSB DCD API for the CH32H417 USBHS peripheral.
 *
 * EP0 DMA strategy (matches official WCH reference):
 *   UEP0_DMA ALWAYS points to usbhs_ep0_buf[64] — never changes.
 *   Both SETUP and OUT data arrive here. The ISR copies data
 *   to the CherryUSB buffer (req_data) at the correct offset.
 *   This eliminates the DMA race between SETUP and DATA phases.
 */

#include "usbd_core.h"
#include "usb_ch32h417_usbhs_reg.h"

#ifndef USB_NUM_BIDIR_ENDPOINTS
#define USB_NUM_BIDIR_ENDPOINTS 8
#endif

/* ----- Endpoint state ----- */
struct ch32h417_usbhs_ep_state {
    uint16_t ep_mps;
    uint8_t  ep_type;
    uint8_t  ep_stalled;
    uint8_t  ep_enable;
    uint8_t *xfer_buf;
    uint32_t xfer_len;
    uint32_t actual_xfer_len;
};

/* EP0 shared DMA buffer — SETUP and OUT data always land here (official pattern) */
__attribute__((aligned(4))) uint8_t usbhs_ep0_buf[64];

/* Pointer to CherryUSB's EP0 data buffer (req_data), set by usbd_ep_start_read */
static uint8_t *ep0_cherry_buf;

/* ----- Driver state ----- */
struct ch32h417_usbhs_udc {
    __attribute__((aligned(4))) struct usb_setup_packet setup;
    volatile uint8_t dev_addr;
    struct ch32h417_usbhs_ep_state in_ep[USB_NUM_BIDIR_ENDPOINTS];
    struct ch32h417_usbhs_ep_state out_ep[USB_NUM_BIDIR_ENDPOINTS];
} g_ch32h417_usbhs_udc;

/* EP0 data toggle state */
volatile bool ep0_rx_data_toggle;
volatile bool ep0_tx_data_toggle;

/* ----- Weak hooks ----- */
__WEAK void usb_dc_low_level_init(void) {}
__WEAK void usb_dc_low_level_deinit(void) {}

/* ----- RCC helper ----- */
static void ch32h417_usbhs_rcc_init(bool enable)
{
    if (enable) {
        if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS) {
            RCC_USBHS_PLLCmd(DISABLE);
            RCC_USBHSPLLCLKConfig(RCC_USBHSPLLSource_HSE);
            RCC_USBHSPLLReferConfig(RCC_USBHSPLLRefer_25M);
            RCC_USBHSPLLClockSourceDivConfig(RCC_USBHSPLL_IN_Div1);
            RCC_USBHS_PLLCmd(ENABLE);
            while (!(RCC->CTLR & RCC_USBHS_PLLRDY)) {}
        }
        RCC_UTMIcmd(ENABLE);
        RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, ENABLE);
    } else {
        RCC_HBPeriphClockCmd(RCC_HBPeriph_USBHS, DISABLE);
        RCC_UTMIcmd(DISABLE);
        if ((RCC->PLLCFGR & RCC_SYSPLL_SEL) != RCC_SYSPLL_USBHS) {
            RCC_USBHS_PLLCmd(DISABLE);
        }
    }
}

/* ----- CherryUSB DCD API ----- */

int usb_dc_init(uint8_t busid)
{
    (void)busid;
    usb_dc_low_level_init();
    ch32h417_usbhs_rcc_init(true);

    USBHSD->CONTROL = USBHS_UD_RST_LINK | USBHS_UD_PHY_SUSPENDM;
    USBHSD->INT_EN = 0;
    USBHSD->INT_FG = 0xFF;
    USBHSD->BASE_MODE = USBHS_UD_SPEED_HIGH;
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    /* EP0 DMA always points to usbhs_ep0_buf (official WCH pattern) */
    USBHSD->UEP0_DMA = (uint32_t)usbhs_ep0_buf;
    USBHSD->UEP_TX_EN = USBHS_UEP0_T_EN;
    USBHSD->UEP_RX_EN = USBHS_UEP0_R_EN;
    USBHSD->UEP_TX_TOG_AUTO = 0;
    USBHSD->UEP_RX_TOG_AUTO = 0;
    USBHSD->UEP_TX_ISO = 0;
    USBHSD->UEP_RX_ISO = 0;
    USBHSD->UEP0_MAX_LEN = 64;
    USBHSD->UEP0_TX_LEN = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
    ep0_rx_data_toggle = true;
    ep0_tx_data_toggle = true;

    USBHSD->INT_EN = USBHS_UDIE_TRANSFER | USBHS_UDIE_BUS_RST |
                     USBHS_UDIE_SUSPEND | USBHS_UDIE_LINK_RDY;
    USBHSD->CONTROL = USBHS_UD_DEV_EN | USBHS_UD_DMA_EN |
                      USBHS_UD_LPM_EN | USBHS_UD_PHY_SUSPENDM;
    NVIC_EnableIRQ(USBHS_IRQn);

    return 0;
}

int usb_dc_deinit(uint8_t busid)
{
    (void)busid;
    NVIC_DisableIRQ(USBHS_IRQn);
    USBHSD->CONTROL = USBHS_UD_RST_SIE | USBHS_UD_RST_LINK;
    ch32h417_usbhs_rcc_init(false);
    usb_dc_low_level_deinit();
    return 0;
}

int usbd_set_address(uint8_t busid, const uint8_t addr)
{
    (void)busid;
    if (addr == 0) USBHSD->DEV_AD = addr & USBHS_UD_DEV_ADDR;
    g_ch32h417_usbhs_udc.dev_addr = addr;
    return 0;
}

int usbd_set_remote_wakeup(uint8_t busid)
{
    (void)busid;
    USBHSD->WAKE_CTRL |= USBHS_UD_REMOTE_WKUP;
    return 0;
}

uint8_t usbd_get_port_speed(uint8_t busid)
{
    (void)busid;
    return (USBHSD->MIS_ST & USBHS_UDMS_HS_MOD) ? USB_SPEED_HIGH : USB_SPEED_FULL;
}

int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *ep)
{
    (void)busid;
    if (ep == NULL) return -1;
    uint8_t ep_idx = USB_EP_GET_IDX(ep->bEndpointAddress);
    uint16_t ep_mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS) return -1;
    if (ep_mps == 0 || (ep_idx == 0 && ep_mps > sizeof(usbhs_ep0_buf)) ||
        (ep_idx != 0 && ep_mps > 1024)) return -1;

    if (USB_EP_DIR_IS_OUT(ep->bEndpointAddress)) {
        g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_mps   = ep_mps;
        g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_type  = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_enable = true;
        USBHSD->UEP_RX_EN |= (1U << ep_idx);
        if (ep_idx > 0) {
            if (g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
                USBHSD->UEP_RX_ISO |= (1U << ep_idx);
                USBHSD->UEP_RX_TOG_AUTO &= ~(1U << ep_idx);
            } else {
                USBHSD->UEP_RX_ISO &= ~(1U << ep_idx);
                USBHSD->UEP_RX_TOG_AUTO |= (1U << ep_idx);
            }
        }
        if (ep_idx == 0)
            USBHSD->UEP0_RX_CTRL = (USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK);
        else
            USBHS_EP_RX_CTRL(ep_idx) = USBHS_UEP_R_RES_NAK;
    } else {
        g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps   = ep_mps;
        g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_type  = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
        g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_enable = true;
        USBHSD->UEP_TX_EN |= (1U << ep_idx);
        if (ep_idx > 0) {
            if (g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
                USBHSD->UEP_TX_ISO |= (1U << ep_idx);
                USBHSD->UEP_TX_TOG_AUTO &= ~(1U << ep_idx);
            } else {
                USBHSD->UEP_TX_ISO &= ~(1U << ep_idx);
                USBHSD->UEP_TX_TOG_AUTO |= (1U << ep_idx);
            }
        }
        if (ep_idx == 0) {
            USBHSD->UEP0_TX_LEN  = 0;
            USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
        } else {
            USBHS_EP_TX_LEN(ep_idx)  = 0;
            USBHS_EP_TX_CTRL(ep_idx) = USBHS_UEP_T_RES_NAK;
        }
    }
    if (ep_idx == 0)
        USBHSD->UEP0_MAX_LEN = ep_mps;
    else
        USBHS_EP_MAX_LEN(ep_idx) = ep_mps;
    return 0;
}

int usbd_ep_close(uint8_t busid, const uint8_t ep)
{
    (void)busid;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS) return -1;
    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0) USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_NAK;
        else USBHS_EP_RX_CTRL(ep_idx) = USBHS_UEP_R_RES_NAK;
        USBHSD->UEP_RX_EN &= ~(1U << ep_idx);
        USBHSD->UEP_RX_TOG_AUTO &= ~(1U << ep_idx);
        USBHSD->UEP_RX_ISO &= ~(1U << ep_idx);
        g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_enable = false;
    } else {
        if (ep_idx == 0) USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
        else USBHS_EP_TX_CTRL(ep_idx) = USBHS_UEP_T_RES_NAK;
        USBHSD->UEP_TX_EN &= ~(1U << ep_idx);
        USBHSD->UEP_TX_TOG_AUTO &= ~(1U << ep_idx);
        USBHSD->UEP_TX_ISO &= ~(1U << ep_idx);
        g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_enable = false;
    }
    return 0;
}

int usbd_ep_set_stall(uint8_t busid, const uint8_t ep)
{
    (void)busid;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS) return -1;
    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0) USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_STALL;
        else USBHS_EP_RX_CTRL(ep_idx) = (USBHS_EP_RX_CTRL(ep_idx) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL;
        g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_stalled = true;
    } else {
        if (ep_idx == 0) USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_STALL;
        else USBHS_EP_TX_CTRL(ep_idx) = (USBHS_EP_TX_CTRL(ep_idx) & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
        g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_stalled = true;
    }
    return 0;
}

int usbd_ep_clear_stall(uint8_t busid, const uint8_t ep)
{
    (void)busid;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS) return -1;
    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0) USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
        else USBHS_EP_RX_CTRL(ep_idx) = USBHS_UEP_R_RES_NAK | USBHS_UEP_R_TOG_DATA0;
        g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_stalled = false;
    } else {
        if (ep_idx == 0) USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
        else USBHS_EP_TX_CTRL(ep_idx) = USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA0;
        g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_stalled = false;
    }
    return 0;
}

int usbd_ep_is_stalled(uint8_t busid, const uint8_t ep, uint8_t *stalled)
{
    (void)busid;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS || stalled == NULL) return -1;
    if (USB_EP_DIR_IS_OUT(ep)) {
        if (ep_idx == 0) *stalled = (USBHSD->UEP0_RX_CTRL & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL ? 1 : 0;
        else *stalled = (USBHS_EP_RX_CTRL(ep_idx) & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL ? 1 : 0;
    } else {
        if (ep_idx == 0) *stalled = (USBHSD->UEP0_TX_CTRL & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL ? 1 : 0;
        else *stalled = (USBHS_EP_TX_CTRL(ep_idx) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL ? 1 : 0;
    }
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t *data, uint32_t data_len)
{
    (void)busid;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS || !USB_EP_DIR_IS_IN(ep)) return -1;
    if (!data && data_len) return -1;
    if (!g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_enable) return -2;
    if (ep_idx != 0 && ((uint32_t)data & 0x03)) return -3;

    g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_buf       = (uint8_t *)data;
    g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_len        = data_len;
    g_ch32h417_usbhs_udc.in_ep[ep_idx].actual_xfer_len = 0;

    if (ep_idx == 0) {
        if (data_len == 0) {
            USBHSD->UEP0_TX_LEN = 0;
        } else {
            uint32_t chunk = MIN(data_len, g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps);
            memcpy(usbhs_ep0_buf, data, chunk);
            USBHSD->UEP0_TX_LEN = chunk;
        }
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_ACK |
                               (ep0_tx_data_toggle ? USBHS_UEP_T_TOG_DATA1 : USBHS_UEP_T_TOG_DATA0);
    } else {
        if (data_len == 0) {
            USBHS_EP_TX_LEN(ep_idx) = 0;
        } else {
            uint32_t chunk = MIN(data_len, g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps);
            USBHS_EP_TX_LEN(ep_idx)  = chunk;
            USBHS_EP_TX_DMA(ep_idx)  = (uint32_t)data;
        }
        USBHS_EP_TX_CTRL(ep_idx) = (USBHS_EP_TX_CTRL(ep_idx) & ~USBHS_UEP_T_RES_MASK) |
                                    USBHS_UEP_T_RES_ACK;
    }
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t *data, uint32_t data_len)
{
    (void)busid;
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    if (ep_idx >= USB_NUM_BIDIR_ENDPOINTS || !USB_EP_DIR_IS_OUT(ep)) return -1;
    if (!data && data_len) return -1;
    if (!g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_enable) return -2;
    if (ep_idx != 0 && ((uint32_t)data & 0x03)) return -3;

    g_ch32h417_usbhs_udc.out_ep[ep_idx].xfer_buf       = (uint8_t *)data;
    g_ch32h417_usbhs_udc.out_ep[ep_idx].xfer_len        = data_len;
    g_ch32h417_usbhs_udc.out_ep[ep_idx].actual_xfer_len = 0;

    if (ep_idx == 0) {
        /* Record CherryUSB buffer position. core passes advanced position
         * on re-arms (ep0_data_buf already advanced by previous packets). */
        ep0_cherry_buf = data;
        if (data_len == 0) {
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK | USBHS_UEP_R_TOG_DATA1;
        } else {
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK |
                                   (ep0_rx_data_toggle ? USBHS_UEP_R_TOG_DATA1 : USBHS_UEP_R_TOG_DATA0);
        }
    } else {
        USBHS_EP_RX_DMA(ep_idx) = (uint32_t)data;
        USBHS_EP_RX_CTRL(ep_idx) = (USBHS_EP_RX_CTRL(ep_idx) & ~USBHS_UEP_R_RES_MASK) |
                                    USBHS_UEP_R_RES_ACK;
    }
    return 0;
}

/* ----- USB Interrupt Handler ----- */
void USBD_IRQHandler(uint8_t busid)
{
    (void)busid;
    uint8_t  intflag, intst, ep_idx;
    uint16_t read_count, write_count;

    intflag = USBHSD->INT_FG;

    if (intflag & USBHS_UDIF_TRANSFER) {
        intst  = USBHSD->INT_ST;
        ep_idx = intst & USBHS_UDIS_EP_ID_MASK;
        if (!(intst & USBHS_UDIS_EP_DIR)) {
            /* ---- OUT or SETUP ---- */
            if (ep_idx == 0) {
                USBHSD->UEP0_RX_CTRL = (USBHSD->UEP0_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
            } else {
                USBHS_EP_RX_CTRL(ep_idx) = (USBHS_EP_RX_CTRL(ep_idx) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                USBHS_EP_RX_CTRL(ep_idx) &= ~USBHS_UEP_R_DONE;
            }

            if (ep_idx == 0 && (USBHSD->UEP0_RX_CTRL & USBHS_UEP_R_SETUP_IS)) {
                /* ---- SETUP Packet ---- */
                USBHSD->UEP0_RX_CTRL &= ~USBHS_UEP_R_DONE;
                /* Setup arrived at usbhs_ep0_buf. */
                memcpy(&g_ch32h417_usbhs_udc.setup, usbhs_ep0_buf, 8);
                USBHSD->UEP0_DMA = (uint32_t)usbhs_ep0_buf;
                ep0_rx_data_toggle = true;
                ep0_tx_data_toggle = true;
                USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;
                USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
                usbd_event_ep0_setup_complete_handler(0, (uint8_t *)&g_ch32h417_usbhs_udc.setup);
            } else if (ep_idx == 0) {
                /* ---- EP0 OUT Data ---- */
                USBHSD->UEP0_RX_CTRL &= ~USBHS_UEP_R_DONE;
                read_count = USBHSD->UEP0_RX_LEN;

                if (read_count > 0) {
                    ep0_rx_data_toggle ^= 1;
                    /* Copy to CherryUSB buffer at current position (core advances
                     * ep0_data_buf per packet and passes advanced position). */
                    memcpy(ep0_cherry_buf, usbhs_ep0_buf, read_count);
                }

                g_ch32h417_usbhs_udc.out_ep[0].actual_xfer_len += read_count;
                g_ch32h417_usbhs_udc.out_ep[0].xfer_len        -= read_count;

                /* Pass per-packet byte count (NOT cumulative) */
                usbd_event_ep_out_complete_handler(0, 0x00, read_count);

                if (read_count == 0) {
                    /* Out status ZLP — restore for next SETUP */
                    USBHSD->UEP0_DMA    = (uint32_t)usbhs_ep0_buf;
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
                    ep0_rx_data_toggle = true;
                    ep0_tx_data_toggle = true;
                }
            } else {
                /* EPn OUT */
                read_count = USBHS_EP_RX_LEN(ep_idx);
                g_ch32h417_usbhs_udc.out_ep[ep_idx].xfer_buf       += read_count;
                g_ch32h417_usbhs_udc.out_ep[ep_idx].actual_xfer_len += read_count;
                g_ch32h417_usbhs_udc.out_ep[ep_idx].xfer_len        -= read_count;
                if ((read_count < g_ch32h417_usbhs_udc.out_ep[ep_idx].ep_mps) ||
                    (g_ch32h417_usbhs_udc.out_ep[ep_idx].xfer_len == 0)) {
                    usbd_event_ep_out_complete_handler(0, ep_idx, g_ch32h417_usbhs_udc.out_ep[ep_idx].actual_xfer_len);
                } else {
                    USBHS_EP_RX_DMA(ep_idx) = (uint32_t)g_ch32h417_usbhs_udc.out_ep[ep_idx].xfer_buf;
                    USBHS_EP_RX_CTRL(ep_idx) = (USBHS_EP_RX_CTRL(ep_idx) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
                }
            }
        } else {
            /* ---- IN ---- */
            if (ep_idx == 0) {
                USBHSD->UEP0_TX_CTRL = (USBHSD->UEP0_TX_CTRL & ~(USBHS_UEP_T_RES_MASK | USBHS_UEP_T_TOG_MASK)) |
                                        USBHS_UEP_T_RES_NAK | USBHS_UEP_T_TOG_DATA0;
            } else {
                USBHS_EP_TX_CTRL(ep_idx) = (USBHS_EP_TX_CTRL(ep_idx) & ~USBHS_UEP_T_RES_MASK) |
                                            USBHS_UEP_T_RES_NAK;
                USBHS_EP_TX_CTRL(ep_idx) &= ~USBHS_UEP_T_DONE;
            }

            if (ep_idx == 0) {
                USBHSD->UEP0_TX_CTRL &= ~USBHS_UEP_T_DONE;
                if (g_ch32h417_usbhs_udc.in_ep[0].xfer_len >= g_ch32h417_usbhs_udc.in_ep[0].ep_mps) {
                    g_ch32h417_usbhs_udc.in_ep[0].xfer_len       -= g_ch32h417_usbhs_udc.in_ep[0].ep_mps;
                    g_ch32h417_usbhs_udc.in_ep[0].actual_xfer_len += g_ch32h417_usbhs_udc.in_ep[0].ep_mps;
                    ep0_tx_data_toggle ^= 1;
                } else {
                    g_ch32h417_usbhs_udc.in_ep[0].actual_xfer_len += g_ch32h417_usbhs_udc.in_ep[0].xfer_len;
                    g_ch32h417_usbhs_udc.in_ep[0].xfer_len         = 0;
                    ep0_tx_data_toggle = true;
                }
                usbd_event_ep_in_complete_handler(0, 0x80, g_ch32h417_usbhs_udc.in_ep[0].actual_xfer_len);

                /* Restore DMA to usbhs_ep0_buf for next SETUP (official pattern — never &setup) */
                USBHSD->UEP0_DMA = (uint32_t)usbhs_ep0_buf;

                if (g_ch32h417_usbhs_udc.dev_addr > 0) {
                    USBHSD->DEV_AD = g_ch32h417_usbhs_udc.dev_addr & USBHS_UD_DEV_ADDR;
                    g_ch32h417_usbhs_udc.dev_addr = 0;
                }

                if (g_ch32h417_usbhs_udc.setup.wLength &&
                    ((g_ch32h417_usbhs_udc.setup.bmRequestType & USB_REQUEST_DIR_MASK) == USB_REQUEST_DIR_OUT)) {
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
                    ep0_tx_data_toggle = true;
                } else if (g_ch32h417_usbhs_udc.setup.wLength == 0) {
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
                    ep0_tx_data_toggle = true;
                }
            } else {
                if (g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_len > g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps) {
                    g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_buf       += g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_len       -= g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps;
                    g_ch32h417_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps;
                    write_count = MIN(g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_len, g_ch32h417_usbhs_udc.in_ep[ep_idx].ep_mps);
                    USBHS_EP_TX_LEN(ep_idx) = write_count;
                    USBHS_EP_TX_DMA(ep_idx) = (uint32_t)g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_buf;
                    USBHS_EP_TX_CTRL(ep_idx) = (USBHS_EP_TX_CTRL(ep_idx) & ~USBHS_UEP_T_RES_MASK) |
                                                USBHS_UEP_T_RES_ACK;
                } else {
                    g_ch32h417_usbhs_udc.in_ep[ep_idx].actual_xfer_len += g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_len;
                    g_ch32h417_usbhs_udc.in_ep[ep_idx].xfer_len         = 0;
                    usbd_event_ep_in_complete_handler(0, ep_idx | 0x80, g_ch32h417_usbhs_udc.in_ep[ep_idx].actual_xfer_len);
                }
            }
        }
    }

    if (intflag & USBHS_UDIF_BUS_RST) {
        USBHSD->INT_FG = USBHS_UDIF_BUS_RST;
        g_ch32h417_usbhs_udc.dev_addr = 0;
        USBHSD->DEV_AD = 0;
        USBHSD->UEP_TX_EN = USBHS_UEP0_T_EN;
        USBHSD->UEP_RX_EN = USBHS_UEP0_R_EN;
        USBHSD->UEP_TX_TOG_AUTO = 0;
        USBHSD->UEP_RX_TOG_AUTO = 0;
        USBHSD->UEP_TX_ISO = 0;
        USBHSD->UEP_RX_ISO = 0;
        USBHSD->UEP0_MAX_LEN = 64;
        USBHSD->UEP0_TX_LEN  = 0;
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
        ep0_tx_data_toggle = true;
        ep0_rx_data_toggle = true;
        for (uint8_t i = 1; i < USB_NUM_BIDIR_ENDPOINTS; i++) {
            USBHS_EP_TX_LEN(i)  = 0;
            USBHS_EP_TX_CTRL(i) = USBHS_UEP_T_RES_NAK;
            USBHS_EP_RX_CTRL(i) = USBHS_UEP_R_RES_NAK;
        }
        memset(&g_ch32h417_usbhs_udc, 0, sizeof(struct ch32h417_usbhs_udc));
        g_ch32h417_usbhs_udc.in_ep[0].ep_mps   = 64;
        g_ch32h417_usbhs_udc.out_ep[0].ep_mps  = 64;
        g_ch32h417_usbhs_udc.in_ep[0].ep_enable  = true;
        g_ch32h417_usbhs_udc.out_ep[0].ep_enable = true;
        USBHSD->INT_FG = 0xFF;
        USBHSD->UEP0_DMA    = (uint32_t)usbhs_ep0_buf;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
        usbd_event_reset_handler(0);
    }

    if (intflag & USBHS_UDIF_SUSPEND) {
        USBHSD->INT_FG = USBHS_UDIF_SUSPEND;
        if (USBHSD->MIS_ST & USBHS_UDMS_SUSPEND)
            usbd_event_suspend_handler(0);
        else
            usbd_event_resume_handler(0);
    }
    if (intflag & USBHS_UDIF_LINK_RDY) {
        USBHSD->INT_FG = USBHS_UDIF_LINK_RDY;
        usbd_event_connect_handler(0);
    }
}

void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBHS_IRQHandler(void) { USBD_IRQHandler(0); }
