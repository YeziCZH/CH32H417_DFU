#!/usr/bin/env python3
"""Extended CH32H417 DFU tests for standard DFU and DfuSe paths.

Keep the board in USBHS/DFU mode before running. The test writes only the
application flash region.
"""

import struct
import sys
import time

import usb.core

import dfu_download as dfu

APP_BASE = dfu.APP_BASE
APP_END = 0x080F0000
ERASE_SIZE = dfu.ERASE_SIZE
XFER_SIZE = dfu.TRANSFER_SIZE


def status(dev):
    return dfu.get_status(dev)


def wait_download_idle(dev, label, limit=30.0):
    deadline = time.time() + limit
    last = None
    while time.time() < deadline:
        stat, timeout_ms, state = status(dev)
        current = (stat, state, timeout_ms)
        if current != last:
            print(f"    {label}: status=0x{stat:02X} state={state} timeout={timeout_ms}ms")
            last = current
        if stat != 0 or state == dfu.DFU_ERROR:
            raise RuntimeError(f"{label}: status=0x{stat:02x} state={state}")
        if state == dfu.DFU_DNLOAD_IDLE:
            return
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError(f"{label}: timeout")


def command(dev, command_id, address):
    payload = struct.pack("<BI", command_id, address)
    dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 0, 0, payload, timeout=5000)
    wait_download_idle(dev, f"cmd 0x{command_id:02X} @ 0x{address:08X}")


def erase(dev, address):
    command(dev, dfu.CMD_ERASE, address)


def set_address(dev, address):
    command(dev, dfu.CMD_SET_ADDRESS, address)


def abort(dev):
    try:
        dev.ctrl_transfer(0x21, dfu.DFU_ABORT, 0, 0, b"", timeout=2000)
    except usb.core.USBError:
        pass


def manifest(dev):
    dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 0, 0, b"", timeout=5000)
    deadline = time.time() + 30.0
    last = None
    while time.time() < deadline:
        try:
            stat, timeout_ms, state = status(dev)
        except usb.core.USBError:
            time.sleep(0.5)
            return dfu.find_device()
        current = (stat, state, timeout_ms)
        if current != last:
            print(f"    manifest: status=0x{stat:02X} state={state} timeout={timeout_ms}ms")
            last = current
        if stat != 0:
            raise RuntimeError(f"manifest status=0x{stat:02x} state={state}")
        if state == dfu.DFU_IDLE:
            return dev
        if state == 8:
            time.sleep(0.05)
            continue
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError("manifest timeout")


def upload(dev, address, length, standard=False):
    first_block = 0 if standard else 2
    if not standard:
        set_address(dev, address)
    result = bytearray()
    block = first_block
    while len(result) < length:
        request = min(XFER_SIZE, length - len(result))
        chunk = bytes(dev.ctrl_transfer(0xA1, dfu.DFU_UPLOAD, block, 0,
                                        request, timeout=5000))
        result.extend(chunk)
        if len(chunk) < request:
            break
        block += 1
    abort(dev)
    return bytes(result)


def download(dev, address, data, standard=False, erase_first=False):
    first_block = 0 if standard else 2
    if erase_first:
        first = address & ~(ERASE_SIZE - 1)
        last = (address + len(data) + ERASE_SIZE - 1) & ~(ERASE_SIZE - 1)
        for sector in range(first, last, ERASE_SIZE):
            erase(dev, sector)
    if not standard:
        set_address(dev, address)
    blocks = (len(data) + XFER_SIZE - 1) // XFER_SIZE
    for index in range(blocks):
        chunk = data[index * XFER_SIZE:(index + 1) * XFER_SIZE]
        dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, first_block + index, 0,
                          chunk, timeout=5000)
        wait_download_idle(dev, f"block {first_block + index}")
    return manifest(dev)


def pattern(length, seed):
    data = bytearray(((i * seed + 0x37) & 0xFF) for i in range(length))
    if length >= 4:
        data[:4] = b"\x13\x00\x00\x00"
    return bytes(data)


def expect(label, ok):
    print(f"  {label}: {'PASS' if ok else 'FAIL'}")
    if not ok:
        raise RuntimeError(label)


def main():
    dev = dfu.find_device()
    print(f"DFU device {dev.idVendor:04x}:{dev.idProduct:04x} speed={dev.speed} status={status(dev)}")

    print("\nT04 blank-sector representation")
    erase(dev, APP_BASE)
    readback = upload(dev, APP_BASE, 512)
    expect("erased words are 39 E3 39 E3", readback[:32] == bytes([0x39, 0xE3]) * 16)

    print("\nT06 standard DFU download/upload")
    image = pattern(512, 7)
    erase(dev, APP_BASE)
    dev = download(dev, APP_BASE, image, standard=True)
    expect("standard 512B readback", upload(dev, APP_BASE, len(image)) == image)

    print("\nT07 DfuSe explicit address")
    image = pattern(512, 11)
    dev = download(dev, 0x08020000, image, erase_first=True)
    expect("DfuSe 512B readback", upload(dev, 0x08020000, len(image)) == image)

    print("\nT07A DfuSe upload transition")
    set_address(dev, 0x08020000)
    chunk = bytes(dev.ctrl_transfer(0xA1, dfu.DFU_UPLOAD, 2, 0, 16, timeout=5000))
    _, _, state = status(dev)
    expect("immediate upload after SET_ADDRESS", len(chunk) == 16 and state == dfu.DFU_UPLOAD_IDLE)
    abort(dev)

    print("\nT08 partial-sector RMW")
    base = 0x08030000
    original = pattern(ERASE_SIZE, 13)
    dev = download(dev, base, original, erase_first=True)
    patch = bytes((0xA0 + i) & 0xFF for i in range(333))
    dev = download(dev, base + 123, patch)
    expected = original[:123] + patch + original[123 + len(patch):]
    expect("unaligned patch preserves surrounding bytes", upload(dev, base, ERASE_SIZE) == expected)

    print("\nT09 sector crossing")
    base = 0x08040000
    image = pattern(9 * 1024, 17)
    dev = download(dev, base, image, erase_first=True)
    expect("9KB cross-sector readback", upload(dev, base, len(image)) == image)

    print("\nT10 last sector and end upload")
    base = 0x080EE000
    image = pattern(2048, 19)
    dev = download(dev, base, image, erase_first=True)
    expect("last sector readback", upload(dev, base, len(image)) == image)
    set_address(dev, APP_END - XFER_SIZE)
    chunk = bytes(dev.ctrl_transfer(0xA1, dfu.DFU_UPLOAD, 3, 0, 512, timeout=5000))
    expect("upload at exact end returns zero length", len(chunk) == 0)
    abort(dev)

    print("\nEXTENDED TESTS COMPLETE")


if __name__ == "__main__":
    sys.exit(main())
