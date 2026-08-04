#ifndef DFU_BOOT_H
#define DFU_BOOT_H

#define DFU_BOOT_MAGIC_ADDRESS 0x201100FCu
#define DFU_BOOT_MAGIC_VALUE   0x44465521u

void dfu_request_reboot(void);

#endif
