/* CH32H417 DFU flash media backend. Flash operations run in the main loop. */
#include "dfu_flash.h"
#include "ch32h417_conf.h"
#include <string.h>

#ifndef DFU_FLASH_DEBUG_LOG
#define DFU_FLASH_DEBUG_LOG 0
#endif

#if DFU_FLASH_DEBUG_LOG
#include "serial.h"
#endif

#define FLASH_PROGRAM_SIZE 0x100u
#define FLASH_WAIT_TIMEOUT 0x0A000000u
#define FLASH_CFGR0_DBMODE (1u << 28)

enum flash_op_result {
    FLASH_OP_OK = 0,
    FLASH_OP_ERASE = -1,
    FLASH_OP_WRITE = -2,
    FLASH_OP_VERIFY = -3,
    FLASH_OP_TARGET = -4,
};

enum flash_job_type {
    FLASH_JOB_NONE = 0,
    FLASH_JOB_WRITE,
    FLASH_JOB_ERASE,
};

struct flash_job {
    volatile enum flash_job_type type;
    uint32_t addr;
    uint16_t len;
    uint16_t pos;
    __attribute__((aligned(4))) uint8_t data[DFU_FLASH_XFER_SIZE];
};

static struct flash_job g_job;
__attribute__((aligned(4))) static uint8_t g_sector[DFU_FLASH_ERASE_SIZE];
static uint32_t g_sector_addr;
static volatile uint8_t g_sector_valid;
static volatile uint8_t g_sector_dirty;
static volatile uint8_t g_finishing;
static volatile uint8_t g_all_done;
static volatile enum dfu_flash_error g_error;
static volatile uint32_t g_bytes_written;
static volatile uint32_t g_bytes_total;

__attribute__((section(".noinit_dfu"))) uint32_t dbg_dfu[6];

enum dfu_dbg_event {
    DBG_FLASH_BEGIN = 1,
    DBG_QUEUE_WRITE,
    DBG_QUEUE_ERASE,
    DBG_POLL_ERASE,
    DBG_ERASE_START,
    DBG_ERASE_DONE,
    DBG_ERASE_FAIL,
    DBG_VERIFY_FAIL,
    DBG_POLL_WRITE,
    DBG_FLUSH_START,
    DBG_FLUSH_DONE,
    DBG_FLASH_ERROR,
};

static void dfu_dbg(enum dfu_dbg_event event, uint32_t addr, int32_t arg)
{
    dbg_dfu[0]++;
    dbg_dfu[1] = (uint32_t)event;
    dbg_dfu[2] = addr;
    dbg_dfu[3] = (uint32_t)arg;
    dbg_dfu[4] = (uint32_t)g_job.type;
    dbg_dfu[5] = (uint32_t)g_error;
}

#if DFU_FLASH_DEBUG_LOG
static void dfu_log(const char *tag, uint32_t addr, int32_t arg)
{
    (void)addr;
    (void)arg;
    serial_puts(tag);
    serial_puts("\r\n");
}
#else
#define dfu_log(tag, addr, arg) ((void)0)
#endif

static int flash_target_valid(void)
{
    return (*(volatile uint32_t *)FLASH_CFGR0_BASE & FLASH_CFGR0_DBMODE) != 0;
}

int dfu_flash_address_valid(uint32_t addr, uint32_t length)
{
    if (addr < DFU_FLASH_APP_BASE || addr >= DFU_FLASH_APP_END)
        return 0;
    return length <= DFU_FLASH_APP_END - addr;
}

static uint32_t flash_read_word(uint32_t addr)
{
    __asm volatile("fence" ::: "memory");
    return *(volatile uint32_t *)(addr & ~3u);
}

static void flash_copy_from(uint8_t *dst, uint32_t src, uint32_t length)
{
    uint32_t copied = 0;
    while (copied < length) {
        uint32_t current = src + copied;
        uint32_t value = flash_read_word(current);
        uint32_t byte = current & 3u;
        while (byte < 4u && copied < length) {
            dst[copied++] = (uint8_t)(value >> (byte * 8u));
            byte++;
        }
    }
}

static int flash_wait_clear(uint32_t mask)
{
    uint32_t timeout = FLASH_WAIT_TIMEOUT;

    while ((FLASH->STATR & mask) != 0) {
        if (--timeout == 0)
            return -1;
    }
    return 0;
}

static void flash_clear_status(void)
{
    FLASH->STATR = FLASH_STATR_EOP | FLASH_STATR_WRPRTERR;
}

static void flash_lock_all(void)
{
    FLASH->CTLR |= FLASH_CTLR_FAST_LOCK | FLASH_CTLR_LOCK;
}

static int flash_prepare(void)
{
    if (!flash_target_valid()) {
        dfu_dbg(DBG_FLASH_ERROR, 0, DFU_FLASH_ERR_TARGET);
        dfu_log("PT", 0, DFU_FLASH_ERR_TARGET);
        return FLASH_OP_TARGET;
    }
    /* EVT FLASH_WaitForLastOperation() checks BSY here. WRBSY is only
     * meaningful while feeding a 256-byte fast-program page. */
    if (flash_wait_clear(FLASH_STATR_BSY) < 0) {
        dfu_dbg(DBG_FLASH_ERROR, 0, FLASH_OP_WRITE);
        dfu_log("PB", 0, FLASH_OP_WRITE);
        return FLASH_OP_WRITE;
    }

    FLASH_Unlock_Fast();
    if ((FLASH->ACTLR & FLASH_ACTLR_EHMOD) != 0) {
        FLASH->ACTLR &= ~FLASH_ACTLR_EHMOD;
        FLASH->CTLR |= FLASH_CTLR_RSENACT;
    }
    flash_clear_status();
    return FLASH_OP_OK;
}

static int flash_erase_sector(uint32_t addr)
{
    uint32_t i;
    int ret;

    dfu_dbg(DBG_ERASE_START, addr, 0);
    dfu_log("ES", addr, 0);
    ret = flash_prepare();
    if (ret != FLASH_OP_OK)
        return (ret == FLASH_OP_TARGET) ? ret : FLASH_OP_ERASE;
    FLASH->ADDR = addr;
    FLASH->CTLR |= FLASH_CTLR_PER;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    ret = flash_wait_clear(FLASH_STATR_BSY);
    FLASH->CTLR &= ~FLASH_CTLR_PER;
    if (ret < 0 || (FLASH->STATR & FLASH_STATR_WRPRTERR) != 0)
        ret = FLASH_OP_ERASE;
    else
        ret = FLASH_OP_OK;
    flash_clear_status();
    flash_lock_all();

    if (ret != FLASH_OP_OK) {
        dfu_dbg(DBG_ERASE_FAIL, addr, ret);
        dfu_log("EF", addr, ret);
        return ret;
    }

    for (i = 0; i < DFU_FLASH_ERASE_SIZE; i += 4) {
        if (flash_read_word(addr + i) != DFU_FLASH_ERASED_WORD) {
            uint32_t value = flash_read_word(addr + i);
            dfu_dbg(DBG_VERIFY_FAIL, addr + i, (int32_t)value);
            dfu_log("EV", addr + i, (int32_t)value);
            return FLASH_OP_VERIFY;
        }
    }
    dfu_dbg(DBG_ERASE_DONE, addr, 0);
    dfu_log("ED", addr, 0);
    return FLASH_OP_OK;
}

static int flash_program_sector(uint32_t addr, const uint8_t *data)
{
    uint32_t block;
    int ret = flash_erase_sector(addr);

    if (ret != FLASH_OP_OK)
        return ret;

    ret = flash_prepare();
    if (ret != FLASH_OP_OK)
        return ret;
    for (block = 0; block < DFU_FLASH_ERASE_SIZE / FLASH_PROGRAM_SIZE; block++) {
        uint32_t target = addr + block * FLASH_PROGRAM_SIZE;
        const uint32_t *source = (const uint32_t *)(data + block * FLASH_PROGRAM_SIZE);
        uint32_t word;

        FLASH->CTLR |= FLASH_CTLR_PAGE_PG;
        if (flash_wait_clear(FLASH_STATR_BSY | FLASH_STATR_WRBSY) < 0) {
            ret = FLASH_OP_WRITE;
            break;
        }
        for (word = 0; word < FLASH_PROGRAM_SIZE / 4u; word++) {
            *(volatile uint32_t *)(target + word * 4u) = source[word];
            __asm volatile("fence" ::: "memory");
            if (flash_wait_clear(FLASH_STATR_WRBSY) < 0) {
                ret = FLASH_OP_WRITE;
                break;
            }
        }
        if (ret != FLASH_OP_OK)
            break;
        FLASH->CTLR |= FLASH_CTLR_PG_STRT;
        if (flash_wait_clear(FLASH_STATR_BSY) < 0 ||
            (FLASH->STATR & FLASH_STATR_WRPRTERR) != 0) {
            ret = FLASH_OP_WRITE;
            break;
        }
        flash_clear_status();
    }
    FLASH->CTLR &= ~FLASH_CTLR_PAGE_PG;
    flash_clear_status();
    flash_lock_all();

    if (ret != FLASH_OP_OK)
        return ret;

    for (block = 0; block < DFU_FLASH_ERASE_SIZE; block += 4) {
        if (flash_read_word(addr + block) !=
            ((const uint32_t *)data)[block / 4u])
            return FLASH_OP_VERIFY;
    }
    return FLASH_OP_OK;
}

static int flush_sector(void)
{
    uint32_t i;
    int same = 1;
    int ret;

    if (!g_sector_valid || !g_sector_dirty)
        return 0;
    dfu_dbg(DBG_FLUSH_START, g_sector_addr, 0);
    dfu_log("FS", g_sector_addr, 0);
    for (i = 0; i < DFU_FLASH_ERASE_SIZE; i += 4) {
        if (flash_read_word(g_sector_addr + i) !=
            ((const uint32_t *)g_sector)[i / 4u]) {
            same = 0;
            break;
        }
    }
    if (same) {
        g_sector_dirty = 0;
        return 0;
    }

    ret = flash_program_sector(g_sector_addr, g_sector);
    if (ret == FLASH_OP_ERASE)
        g_error = DFU_FLASH_ERR_ERASE;
    else if (ret == FLASH_OP_WRITE)
        g_error = DFU_FLASH_ERR_WRITE;
    else if (ret == FLASH_OP_VERIFY)
        g_error = DFU_FLASH_ERR_VERIFY;
    else if (ret == FLASH_OP_TARGET)
        g_error = DFU_FLASH_ERR_TARGET;
    if (ret != 0)
        return -1;
    g_sector_dirty = 0;
    dfu_dbg(DBG_FLUSH_DONE, g_sector_addr, 0);
    dfu_log("FD", g_sector_addr, 0);
    return 1;
}

void dfu_flash_begin(void)
{
    g_job.type = FLASH_JOB_NONE;
    g_sector_valid = 0;
    g_sector_dirty = 0;
    g_finishing = 0;
    g_all_done = 0;
    g_error = flash_target_valid() ? DFU_FLASH_ERR_NONE : DFU_FLASH_ERR_TARGET;
    g_bytes_written = 0;
    g_bytes_total = 0;
    memset(dbg_dfu, 0, sizeof(dbg_dfu));
    dfu_dbg(DBG_FLASH_BEGIN, 0, g_error);
    dfu_log("BE", 0, g_error);
}

void dfu_flash_cancel(void)
{
    g_job.type = FLASH_JOB_NONE;
    g_sector_valid = 0;
    g_sector_dirty = 0;
    g_finishing = 0;
    g_all_done = 0;
    g_error = flash_target_valid() ? DFU_FLASH_ERR_NONE : DFU_FLASH_ERR_TARGET;
}

int dfu_flash_queue_write(uint32_t addr, const uint8_t *data, uint16_t length)
{
    if (!flash_target_valid()) {
        g_error = DFU_FLASH_ERR_TARGET;
        dfu_dbg(DBG_FLASH_ERROR, addr, g_error);
        dfu_log("WT", addr, g_error);
        return -1;
    }
    if (length == 0 || length > DFU_FLASH_XFER_SIZE || data == NULL ||
        !dfu_flash_address_valid(addr, length)) {
        g_error = DFU_FLASH_ERR_ADDRESS;
        dfu_dbg(DBG_FLASH_ERROR, addr, g_error);
        dfu_log("WA", addr, g_error);
        return -1;
    }
    if (g_job.type != FLASH_JOB_NONE || g_finishing) {
        g_error = DFU_FLASH_ERR_BUSY;
        dfu_dbg(DBG_FLASH_ERROR, addr, g_error);
        dfu_log("WB", addr, g_error);
        return -1;
    }

    memcpy(g_job.data, data, length);
    g_job.addr = addr;
    g_job.len = length;
    g_job.pos = 0;
    g_bytes_total += length;
    dfu_dbg(DBG_QUEUE_WRITE, addr, length);
    dfu_log("QW", addr, length);
    g_job.type = FLASH_JOB_WRITE;
    return 0;
}

int dfu_flash_queue_erase(uint32_t addr)
{
    if (!flash_target_valid()) {
        g_error = DFU_FLASH_ERR_TARGET;
        dfu_dbg(DBG_FLASH_ERROR, addr, g_error);
        dfu_log("ET", addr, g_error);
        return -1;
    }
    if (!dfu_flash_address_valid(addr, DFU_FLASH_ERASE_SIZE) ||
        (addr & (DFU_FLASH_ERASE_SIZE - 1u)) != 0) {
        g_error = DFU_FLASH_ERR_ADDRESS;
        dfu_dbg(DBG_FLASH_ERROR, addr, g_error);
        dfu_log("EA", addr, g_error);
        return -1;
    }
    if (g_job.type != FLASH_JOB_NONE || g_finishing) {
        g_error = DFU_FLASH_ERR_BUSY;
        dfu_dbg(DBG_FLASH_ERROR, addr, g_error);
        dfu_log("EB", addr, g_error);
        return -1;
    }
    g_job.addr = addr;
    g_job.len = 0;
    g_job.pos = 0;
    dfu_dbg(DBG_QUEUE_ERASE, addr, 0);
    dfu_log("QE", addr, 0);
    g_job.type = FLASH_JOB_ERASE;
    return 0;
}

void dfu_flash_finish(void)
{
    g_finishing = 1;
    g_all_done = 0;
}

int dfu_flash_poll(void)
{
    if (g_error != DFU_FLASH_ERR_NONE)
        return -1;

    if (g_job.type == FLASH_JOB_ERASE) {
        int ret;
        uint32_t erase_addr = g_job.addr;
        dfu_dbg(DBG_POLL_ERASE, erase_addr, 0);
        dfu_log("PE", erase_addr, 0);
        if (g_sector_valid && g_sector_addr == g_job.addr) {
            g_sector_valid = 0;
            g_sector_dirty = 0;
        } else if (flush_sector() < 0) {
            return -1;
        }
        ret = flash_erase_sector(erase_addr);
        g_job.type = FLASH_JOB_NONE;
        if (ret != 0) {
            if (ret == FLASH_OP_VERIFY)
                g_error = DFU_FLASH_ERR_VERIFY;
            else if (ret == FLASH_OP_TARGET)
                g_error = DFU_FLASH_ERR_TARGET;
            else
                g_error = DFU_FLASH_ERR_ERASE;
            dfu_dbg(DBG_FLASH_ERROR, erase_addr, g_error);
            dfu_log("PX", erase_addr, g_error);
            return -1;
        }
        dfu_dbg(DBG_ERASE_DONE, erase_addr, 1);
        dfu_log("PD", erase_addr, 1);
        return 1;
    }

    if (g_job.type == FLASH_JOB_WRITE) {
        uint32_t addr = g_job.addr + g_job.pos;
        uint32_t sector = addr & ~(DFU_FLASH_ERASE_SIZE - 1u);
        uint32_t offset = addr - sector;
        uint32_t remain = g_job.len - g_job.pos;
        uint32_t chunk = DFU_FLASH_ERASE_SIZE - offset;

        dfu_dbg(DBG_POLL_WRITE, addr, remain);
        if (chunk > remain)
            chunk = remain;
        if (!g_sector_valid || g_sector_addr != sector) {
            if (flush_sector() < 0)
                return -1;
            flash_copy_from(g_sector, sector, DFU_FLASH_ERASE_SIZE);
            g_sector_addr = sector;
            g_sector_valid = 1;
            g_sector_dirty = 0;
        }
        memcpy(g_sector + offset, g_job.data + g_job.pos, chunk);
        g_sector_dirty = 1;
        g_job.pos += (uint16_t)chunk;
        if (g_job.pos == g_job.len) {
            g_bytes_written += g_job.len;
            g_job.type = FLASH_JOB_NONE;
        }
        return 1;
    }

    if (g_finishing) {
        int ret = flush_sector();
        if (ret < 0)
            return -1;
        g_finishing = 0;
        g_all_done = 1;
        return ret ? 1 : 0;
    }
    return 0;
}

int dfu_flash_busy(void)
{
    return g_job.type != FLASH_JOB_NONE || g_finishing;
}

int dfu_flash_all_done(void)
{
    return g_all_done;
}

enum dfu_flash_error dfu_flash_get_error(void)
{
    return g_error;
}

void dfu_flash_progress(uint32_t *written, uint32_t *total)
{
    *written = g_bytes_written;
    *total = g_bytes_total;
}

void dfu_flash_fill_poll_timeout(uint8_t buf[3])
{
    uint32_t timeout = (g_job.type == FLASH_JOB_WRITE && !g_finishing) ? 2u : 20u;
    buf[0] = (uint8_t)timeout;
    buf[1] = (uint8_t)(timeout >> 8);
    buf[2] = (uint8_t)(timeout >> 16);
}

int dfu_flash_read(uint32_t addr, uint8_t *data, uint16_t length,
                   uint16_t *actual_length)
{
    uint32_t available;
    uint16_t count;

    if (!flash_target_valid()) {
        g_error = DFU_FLASH_ERR_TARGET;
        return -1;
    }
    if (addr < DFU_FLASH_APP_BASE || addr > DFU_FLASH_APP_END || data == NULL ||
        actual_length == NULL) {
        g_error = DFU_FLASH_ERR_ADDRESS;
        return -1;
    }
    if (addr == DFU_FLASH_APP_END) {
        *actual_length = 0;
        return 0;
    }
    available = DFU_FLASH_APP_END - addr;
    count = (available < length) ? (uint16_t)available : length;
    flash_copy_from(data, addr, count);
    *actual_length = count;
    return 0;
}
