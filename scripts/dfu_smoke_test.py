#!/usr/bin/env python3
"""Targeted DFU smoke tests for CH32H417 bootloader.

This script intentionally writes only the application flash region.
Keep the board in USBHS/DFU mode before running.
"""

from pathlib import Path
import struct
import time

import usb.core
import usb.util

import dfu_download as dfu

ERR_ADDRESS = 0x08
ERR_NOTDONE = 0x09


def make_pattern(length, seed, entry=True):
    data = bytearray(((i * seed + (seed ^ 0x5A)) & 0xFF) for i in range(length))
    if entry and length >= 4:
        data[0:4] = b"\xef\xbe\xad\xde"
    return bytes(data)


class DfuSession:
    def __init__(self):
        self.dev = None

    def open(self, wait=True):
        deadline = time.time() + 10.0
        last = None
        while True:
            try:
                self.dev = dfu.find_device()
                return self.dev
            except Exception as exc:
                last = exc
                if not wait or time.time() >= deadline:
                    raise RuntimeError(f"DFU device unavailable: {last}") from exc
                time.sleep(0.2)

    def close(self):
        if self.dev is not None:
            try:
                usb.util.dispose_resources(self.dev)
            except Exception:
                pass
            self.dev = None

    def reopen(self):
        self.close()
        time.sleep(0.5)
        return self.open(wait=True)

    def status(self):
        return dfu.get_status(self.dev)

    def wait_download_idle(self, label):
        last = None
        deadline = time.time() + 30.0
        polls = 0
        while time.time() < deadline:
            status, timeout_ms, state = self.status()
            current = (status, state, timeout_ms)
            if current != last:
                print(f"    {label}: status=0x{status:02X} state={state} timeout={timeout_ms}ms")
                last = current
            if status != 0 or state == dfu.DFU_ERROR:
                raise RuntimeError(f"{label}: DFU error status=0x{status:02x}, state={state}")
            if state == dfu.DFU_DNLOAD_IDLE:
                return
            time.sleep(max(timeout_ms, 1) / 1000.0)
            polls += 1
        raise RuntimeError(f"{label}: timeout waiting for dfuDNLOAD-IDLE after {polls} polls")

    def clear_status(self):
        self.dev.ctrl_transfer(0x21, dfu.DFU_CLRSTATUS, 0, 0, b"", timeout=2000)

    def abort(self):
        try:
            self.dev.ctrl_transfer(0x21, dfu.DFU_ABORT, 0, 0, b"", timeout=2000)
        except usb.core.USBError:
            pass

    def command_raw(self, command, address):
        payload = struct.pack("<BI", command, address)
        self.dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 0, 0, payload, timeout=5000)

    def command(self, command, address):
        self.command_raw(command, address)
        self.wait_download_idle(f"cmd 0x{command:02X} @ 0x{address:08X}")

    def erase(self, address):
        self.command(dfu.CMD_ERASE, address)

    def set_address(self, address):
        self.command(dfu.CMD_SET_ADDRESS, address)

    def download_bytes(self, address, data, erase=False, standard=False, manifest=True):
        if standard:
            first_block = 0
        else:
            if erase:
                first = address & ~(dfu.ERASE_SIZE - 1)
                last = (address + len(data) + dfu.ERASE_SIZE - 1) & ~(dfu.ERASE_SIZE - 1)
                for sector in range(first, last, dfu.ERASE_SIZE):
                    self.erase(sector)
            self.set_address(address)
            first_block = 2

        blocks = (len(data) + dfu.TRANSFER_SIZE - 1) // dfu.TRANSFER_SIZE
        for index in range(blocks):
            chunk = data[index * dfu.TRANSFER_SIZE:(index + 1) * dfu.TRANSFER_SIZE]
            self.dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, first_block + index, 0, chunk, timeout=5000)
            self.wait_download_idle(f"block {first_block + index}")

        if manifest:
            self.dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 0, 0, b"", timeout=5000)
            for _ in range(250):
                try:
                    status, timeout_ms, state = self.status()
                except usb.core.USBError:
                    self.reopen()
                    return
                if status != 0:
                    raise RuntimeError(f"manifest failed: status=0x{status:02x}, state={state}")
                if state == 8:
                    self.reopen()
                    return
                time.sleep(max(timeout_ms, 1) / 1000.0)
            raise RuntimeError("manifest timeout")

    def upload_bytes(self, address, length):
        self.set_address(address)
        result = bytearray()
        block = 2
        while len(result) < length:
            request = min(dfu.TRANSFER_SIZE, length - len(result))
            chunk = bytes(self.dev.ctrl_transfer(0xA1, dfu.DFU_UPLOAD, block, 0, request, timeout=5000))
            result.extend(chunk)
            if len(chunk) < request:
                break
            block += 1
        self.abort()
        return bytes(result)


def expect(name, condition):
    if not condition:
        raise RuntimeError(f"{name}: FAIL")
    print(f"{name}: PASS")


def test_t05_invalid_erase(s):
    print("\nT05 erase range/alignment")
    s.erase(dfu.APP_BASE)
    print(f"  aligned erase 0x{dfu.APP_BASE:08X}: OK")
    for addr in (dfu.APP_BASE + 0x100, 0x08000000,
                 dfu.APP_BASE - dfu.ERASE_SIZE, 0x080F0000):
        s.command_raw(dfu.CMD_ERASE, addr)
        status, _, state = s.status()
        print(f"  invalid erase 0x{addr:08X}: status=0x{status:02X}, state={state}")
        expect(f"  reject 0x{addr:08X}", status == ERR_ADDRESS and state == dfu.DFU_ERROR)
        s.clear_status()
        status, _, state = s.status()
        expect(f"  CLRSTATUS recovers 0x{addr:08X}", status == 0 and state == dfu.DFU_IDLE)


def test_t11_differential(s):
    print("\nT11 differential update")
    base = 0x08050000
    original = make_pattern(0x4000, 23)
    s.download_bytes(base, original, erase=True)
    rb = s.upload_bytes(base, len(original))
    expect("  initial image readback", rb == original)

    s.download_bytes(base, original, erase=False)
    rb2 = s.upload_bytes(base, len(original))
    expect("  identical second download preserved", rb2 == original)

    changed = bytearray(original)
    patch_offset = 0x2300
    patch = make_pattern(256, 41, entry=False)
    changed[patch_offset:patch_offset + len(patch)] = patch
    s.download_bytes(base + patch_offset, patch, erase=False)
    rb3 = s.upload_bytes(base, len(changed))
    expect("  one-sector patch readback", rb3 == bytes(changed))
    expect("  preceding sector preserved", rb3[:0x2000] == original[:0x2000])


def test_t12_command_blocks(s):
    print("\nT12 command block disambiguation")
    base = 0x08060000
    length = (82 * dfu.TRANSFER_SIZE)
    image = bytearray(make_pattern(length, 31))
    for block, marker in ((33, b"BLK33"), (65, b"BLK65"), (81, b"BLK81")):
        off = block * dfu.TRANSFER_SIZE
        image[off:off + len(marker)] = marker
    s.download_bytes(base, bytes(image), erase=True)
    rb = s.upload_bytes(base, len(image))
    expect("  blocks 33/65/81 are data", rb == bytes(image))


def test_t13_abort(s):
    print("\nT13 abort and recovery")
    base = 0x08074000
    initial = make_pattern(0x2000, 37)
    s.download_bytes(base, initial, erase=True)
    expect("  initial sector", s.upload_bytes(base, len(initial)) == initial)

    partial = make_pattern(128, 43, entry=False)
    s.set_address(base + 0x123)
    s.dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 2, 0, partial, timeout=5000)
    s.wait_download_idle("partial block before ABORT")
    s.abort()
    time.sleep(0.1)
    expect("  ABORT did not flush dirty cache", s.upload_bytes(base, len(initial)) == initial)

    final = make_pattern(0x2000, 47)
    s.download_bytes(base, final, erase=True)
    expect("  new full download after ABORT", s.upload_bytes(base, len(final)) == final)


def test_t14_error_recovery(s):
    print("\nT14 error recovery")
    s.command_raw(dfu.CMD_ERASE, 0x08000000)
    status, _, state = s.status()
    print(f"  invalid bootloader erase: status=0x{status:02X}, state={state}")
    expect("  error status reported", status == ERR_ADDRESS and state == dfu.DFU_ERROR)
    s.clear_status()
    status, _, state = s.status()
    expect("  CLRSTATUS returns to idle", status == 0 and state == dfu.DFU_IDLE)

    base = 0x08078000
    data = make_pattern(512, 59)
    s.download_bytes(base, data, erase=True)
    expect("  valid operation after error", s.upload_bytes(base, len(data)) == data)


def main():
    s = DfuSession()
    s.open()
    print(f"DFU device opened: {hex(s.dev.idVendor)}:{hex(s.dev.idProduct)} speed={s.dev.speed}")
    try:
        test_t05_invalid_erase(s)
        test_t11_differential(s)
        test_t12_command_blocks(s)
        test_t13_abort(s)
        test_t14_error_recovery(s)
    finally:
        s.close()
    print("\nSMOKE TESTS COMPLETE")


if __name__ == "__main__":
    main()
