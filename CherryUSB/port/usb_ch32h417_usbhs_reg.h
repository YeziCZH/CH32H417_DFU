/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CH32H417 USBHS (USB 2.0 High-Speed) Device Register Helpers
 *
 * All bit-level macros (USBHS_UEP_T_RES_ACK, USBHS_UDIE_TRANSFER, etc.)
 * are already defined in ch32h417_usb.h (included via ch32h417.h).
 *
 * This header only adds EP indexed access macros for EP1-EP7,
 * which the SDK does not provide as generic index-based accessors.
 */
#ifndef _USB_CH32H417_USBHS_REG_H
#define _USB_CH32H417_USBHS_REG_H

#include "ch32h417.h"
#include "ch32h417_usb.h"   /* bit-level macros: USBHS_UD_*, USBHS_UEP_*, etc. */

/* ========== EP1-EP7 Indexed Access Macros ========== */
/*
 * EP0 has dedicated named registers in USBHSD_TypeDef:
 *   UEP0_DMA, UEP0_MAX_LEN, UEP0_TX_LEN, UEP0_TX_CTRL, UEP0_RX_CTRL.
 *
 * EP1~EP7 are addressed via fixed offsets from USBHSD_BASE.
 * These macros allow indexed access: USBHS_EP_TX_CTRL(3) → UEP3_TX_CTRL.
 */

#define USBHSD_EP_RXDMA_BASE   (USBHSD_BASE + 0x24U)
#define USBHSD_EP_TXDMA_BASE   (USBHSD_BASE + 0x40U)
#define USBHSD_EP_MAXLEN_BASE  (USBHSD_BASE + 0x5CU)
#define USBHSD_EP_TXLEN_BASE   (USBHSD_BASE + 0xA0U)
#define USBHSD_EP_TXCTRL_BASE  (USBHSD_BASE + 0xA2U)
#define USBHSD_EP_RXCTRL_BASE  (USBHSD_BASE + 0xA3U)
#define USBHSD_EP_RXLEN_BASE   (USBHSD_BASE + 0x7CU)

#define USBHS_EP_RX_DMA(n)     (*(volatile uint32_t *)(USBHSD_EP_RXDMA_BASE  + ((n) - 1U) * 4U))
#define USBHS_EP_TX_DMA(n)     (*(volatile uint32_t *)(USBHSD_EP_TXDMA_BASE  + ((n) - 1U) * 4U))
#define USBHS_EP_MAX_LEN(n)    (*(volatile uint32_t *)(USBHSD_EP_MAXLEN_BASE + ((n)      ) * 4U))
#define USBHS_EP_TX_LEN(n)     (*(volatile uint16_t *)(USBHSD_EP_TXLEN_BASE  + ((n) - 1U) * 4U))
#define USBHS_EP_TX_CTRL(n)    (*(volatile  uint8_t *)(USBHSD_EP_TXCTRL_BASE + ((n) - 1U) * 4U))
#define USBHS_EP_RX_CTRL(n)    (*(volatile  uint8_t *)(USBHSD_EP_RXCTRL_BASE + ((n) - 1U) * 4U))
#define USBHS_EP_RX_LEN(n)     (*(volatile uint16_t *)(USBHSD_EP_RXLEN_BASE  + ((n)      ) * 4U))

#endif /* _USB_CH32H417_USBHS_REG_H */
