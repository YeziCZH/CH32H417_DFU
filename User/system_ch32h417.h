#pragma once

#include "ch32h417.h"

extern uint32_t HCLKClock;
extern uint32_t SystemClock;
extern uint32_t SystemCoreClock;

void SystemInit(void);
void SystemAndCoreClockUpdate(void);
