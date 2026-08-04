#!/usr/bin/env python3
"""Full release-safe DFU regression for the CH32H417 16-KB layout.

Tests T01 and T03-T14 plus T17 from TEST_PLAN.md. T15/T16 use executable
jump images and are run separately so COM4 can be captured. T18 requires a
manual power interruption.
"""

from pathlib import Path
import argparse
import time

import usb.util

import dfu_download as dfu
from dfu_test_support import (
    APP_END,
    ERASED_32,
    ERR_ADDRESS,
    SAFE_EXEC,
    DfuSession,
    download,
    expect,
    expect_idle,
    make_pattern,
    upload,
)


def test_t01():
    print("\nT01 firmware size")
    image = Path(__file__).resolve().parents[1] / "build" / "ch32h417_dfu.bin"
    size = image.stat().st_size
    print(f"  {image.name}: {size} bytes")
    expect("  release image <= 16 KB", size <= 16 * 1024)


def test_t03(session):
    print("\nT03 USB enumeration")
    config = session.dev.get_active_configuration()
    interface = config[(0, 0)]
    descriptor = usb.util.get_string(session.dev, interface.iInterface)
    expect("  VID:PID", (session.dev.idVendor, session.dev.idProduct) ==
           (dfu.VID, dfu.PID))
    expect("  USB High-Speed", session.dev.speed == 3)
    expect("  16-KB DfuSe descriptor",
           descriptor == "@Flash/0x08004000/118*8Kg")
    expect_idle(session, "  initial dfuIDLE")


def test_t04(session):
    print("\nT04 blank-sector representation")
    session.erase(dfu.APP_BASE)
    expect("  erased word is 0xE339E339",
           upload(session, dfu.APP_BASE, 32) == ERASED_32)


def test_t05(session):
    print("\nT05 erase range/alignment and target gate")
    session.erase(dfu.APP_BASE)
    expect("  runtime target accepts valid Flash operation", True)
    for address in (dfu.APP_BASE + 0x100, 0x08000000,
                    dfu.APP_BASE - dfu.ERASE_SIZE, APP_END):
        session.command_raw(dfu.CMD_ERASE, address)
        status, _, state = session.status()
        expect(f"  reject 0x{address:08X}",
               status == ERR_ADDRESS and state == dfu.DFU_ERROR)
        session.clear_status()
        expect_idle(session, f"  recover 0x{address:08X}")


def test_t06(session):
    print("\nT06 standard DFU manifestation and Upload")
    data = make_pattern(512, 7)
    session.erase(dfu.APP_BASE)
    download(session, dfu.APP_BASE, data, standard=True)
    expect("  standard block 0 readback",
           upload(session, dfu.APP_BASE, len(data), standard=True) == data)


def test_t07(session):
    print("\nT07 DfuSe explicit address")
    base = 0x08020000
    data = make_pattern(512, 11)
    download(session, base, data, erase_first=True)
    expect("  DfuSe block 2 readback", upload(session, base, len(data)) == data)

    print("\nT07A immediate DfuSe Upload transition")
    session.set_address(base)
    chunk = bytes(session.dev.ctrl_transfer(
        0xA1, dfu.DFU_UPLOAD, 2, 0, 16, timeout=5000))
    status, _, state = session.status()
    expect("  SET_ADDRESS -> UPLOAD", len(chunk) == 16 and status == 0 and
           state == dfu.DFU_UPLOAD_IDLE)
    session.abort()


def test_t08(session):
    print("\nT08 partial-sector Read-Modify-Write")
    base = 0x08030000
    original = make_pattern(dfu.ERASE_SIZE, 13)
    download(session, base, original, erase_first=True)
    patch = bytes((0xA0 + index) & 0xFF for index in range(333))
    download(session, base + 123, patch)
    expected = original[:123] + patch + original[123 + len(patch):]
    expect("  prefix/suffix preserved",
           upload(session, base, len(expected)) == expected)


def test_t09(session):
    print("\nT09 9-KB sector crossing")
    base = 0x08040000
    data = make_pattern(9 * 1024, 17)
    download(session, base, data, erase_first=True)
    expect("  cross-sector readback", upload(session, base, len(data)) == data)


def test_t10(session):
    print("\nT10 last Flash sector and end boundary")
    base = 0x080EE000
    data = make_pattern(2048, 19)
    download(session, base, data, erase_first=True)
    expect("  last-sector readback", upload(session, base, len(data)) == data)

    session.set_address(APP_END - dfu.TRANSFER_SIZE)
    tail = bytes(session.dev.ctrl_transfer(
        0xA1, dfu.DFU_UPLOAD, 2, 0, dfu.TRANSFER_SIZE, timeout=5000))
    end = bytes(session.dev.ctrl_transfer(
        0xA1, dfu.DFU_UPLOAD, 3, 0, dfu.TRANSFER_SIZE, timeout=5000))
    expect("  final 512-byte read", len(tail) == dfu.TRANSFER_SIZE)
    expect("  exact-end Upload terminates", len(end) == 0)
    session.abort()


def test_t11(session):
    print("\nT11 differential update")
    base = 0x08050000
    original = make_pattern(0x4000, 23)
    start = time.perf_counter()
    download(session, base, original, erase_first=True)
    first_time = time.perf_counter() - start
    expect("  initial image", upload(session, base, len(original)) == original)

    start = time.perf_counter()
    download(session, base, original)
    same_time = time.perf_counter() - start
    expect("  identical image preserved",
           upload(session, base, len(original)) == original)

    changed = bytearray(original)
    patch_offset = 0x2300
    patch = make_pattern(256, 41, entry=False)
    changed[patch_offset:patch_offset + len(patch)] = patch
    download(session, base + patch_offset, patch)
    readback = upload(session, base, len(changed))
    expect("  one-sector patch", readback == bytes(changed))
    expect("  untouched sector preserved",
           readback[:0x2000] == original[:0x2000])
    print(f"  timing: initial={first_time:.3f}s identical={same_time:.3f}s")


def test_t12(session):
    print("\nT12 command block disambiguation")
    length = 82 * dfu.TRANSFER_SIZE
    image = bytearray(make_pattern(length, 31))
    for block, marker in ((33, b"BLK33"), (65, b"BLK65"), (81, b"BLK81")):
        offset = block * dfu.TRANSFER_SIZE
        image[offset:offset + len(marker)] = marker
    download(session, dfu.APP_BASE, bytes(image), standard=True,
             erase_first=True)
    expect("  standard blocks 33/65/81 remain data",
           upload(session, dfu.APP_BASE, len(image), standard=True) ==
           bytes(image))


def test_t13(session):
    print("\nT13 ABORT and recovery")
    base = 0x08074000
    initial = make_pattern(dfu.ERASE_SIZE, 37)
    download(session, base, initial, erase_first=True)
    expect("  initial sector", upload(session, base, len(initial)) == initial)

    patch = make_pattern(128, 43, entry=False)
    session.set_address(base + 0x123)
    session.dev.ctrl_transfer(
        0x21, dfu.DFU_DNLOAD, 2, 0, patch, timeout=5000)
    session.wait_download_idle("partial block before ABORT")
    session.abort()
    expect("  dirty cache discarded",
           upload(session, base, len(initial)) == initial)

    final = make_pattern(dfu.ERASE_SIZE, 47)
    download(session, base, final, erase_first=True)
    expect("  fresh download after ABORT",
           upload(session, base, len(final)) == final)


def test_t14(session):
    print("\nT14 error recovery")
    session.command_raw(dfu.CMD_ERASE, 0x08000000)
    status, _, state = session.status()
    expect("  errADDRESS reported",
           status == ERR_ADDRESS and state == dfu.DFU_ERROR)
    session.clear_status()
    expect_idle(session, "  CLRSTATUS returns to idle")

    base = 0x08078000
    data = make_pattern(512, 59)
    download(session, base, data, erase_first=True)
    expect("  valid operation after error",
           upload(session, base, len(data)) == data)


def test_t17(session):
    print("\nT17 blank-app protection")
    session.erase(dfu.APP_BASE)
    session.command(dfu.CMD_SET_EXEC_ADDRESS, dfu.APP_BASE)
    session.dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 0, 0, b"", timeout=5000)
    deadline = time.time() + 5.0
    while time.time() < deadline:
        status, timeout_ms, state = session.status()
        if status == 0 and state == dfu.DFU_IDLE:
            expect("  blank APP remains in DFU", True)
            return
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError("blank APP did not return to dfuIDLE")


def main():
    parser = argparse.ArgumentParser(
        description="Run the release-safe 16-KB CH32H417 DFU regression")
    parser.parse_args()

    test_t01()
    session = DfuSession()
    session.open()
    try:
        print(f"DFU {session.dev.idVendor:04X}:{session.dev.idProduct:04X} "
              f"speed={session.dev.speed}")
        session.erase(SAFE_EXEC)
        session.abort()
        test_t03(session)
        test_t04(session)
        test_t05(session)
        test_t06(session)
        test_t07(session)
        test_t08(session)
        test_t09(session)
        test_t10(session)
        test_t11(session)
        test_t12(session)
        test_t13(session)
        test_t14(session)
        test_t17(session)
    finally:
        session.close()
    print("\nFULL 16-KB DFU REGRESSION COMPLETE")


if __name__ == "__main__":
    main()
