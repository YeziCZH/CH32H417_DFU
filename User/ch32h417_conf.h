#pragma once
/* CH32H417 Peripheral header includes — minimal set for DFU bootloader */

#include "ch32h417.h"

/* Core peripherals needed */
#include "ch32h417_rcc.h"
#include "ch32h417_gpio.h"
#include "ch32h417_usart.h"
#include "ch32h417_flash.h"
#include "ch32h417_exti.h"
#include "ch32h417_pwr.h"
#include "ch32h417_rtc.h"
#include "ch32h417_dma.h"
/* misc.h not available; NVIC_SystemReset in core_riscv.h */
