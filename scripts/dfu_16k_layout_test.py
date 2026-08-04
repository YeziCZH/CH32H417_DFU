#!/usr/bin/env python3
"""Release-safe hardware test for the 16-KB DFU bootloader layout.

The test flushes standard DFU data by crossing an 8-KB cache boundary and
uses ABORT instead of manifestation, so arbitrary test data is never run.
Both touched sectors are erased before returning.
"""

import argparse
import time

import usb.util

import dfu_download as dfu
from dfu_test_support import (
    DfuSession,
    ERR_ADDRESS,
    expect,
    expect_idle,
    make_pattern,
    upload,
)


def test_descriptor(session):
    config = session.dev.get_active_configuration()
    interface = config[(0, 0)]
    text = usb.util.get_string(session.dev, interface.iInterface)
    expect("HS enumeration", session.dev.speed == 3)
    expect("16-KB DfuSe descriptor",
           text == "@Flash/0x08004000/118*8Kg")
    expect_idle(session, "initial dfuIDLE")


def test_boundary(session):
    print("\n16-KB boundary protection")
    session.erase(dfu.APP_BASE)
    print(f"  erase first APP sector 0x{dfu.APP_BASE:08X}: OK")

    invalid = (
        dfu.APP_BASE + 0x100,
        0x08000000,
        dfu.APP_BASE - dfu.ERASE_SIZE,
        0x080F0000,
    )
    for address in invalid:
        session.command_raw(dfu.CMD_ERASE, address)
        status, _, state = session.status()
        expect(f"reject erase 0x{address:08X}",
               status == ERR_ADDRESS and state == dfu.DFU_ERROR)
        session.clear_status()
        expect_idle(session, f"recover after 0x{address:08X}")


def test_standard_download_upload(session):
    print("\nStandard DFU at new APP base")
    base = dfu.APP_BASE
    image = make_pattern(dfu.ERASE_SIZE, 67)
    guard = make_pattern(dfu.TRANSFER_SIZE, 71, entry=False)

    session.erase(base)
    session.erase(base + dfu.ERASE_SIZE)

    blocks = len(image) // dfu.TRANSFER_SIZE
    for block in range(blocks):
        start = block * dfu.TRANSFER_SIZE
        chunk = image[start:start + dfu.TRANSFER_SIZE]
        session.dev.ctrl_transfer(
            0x21, dfu.DFU_DNLOAD, block, 0, chunk, timeout=5000)
        session.wait_download_idle(f"standard block {block}")

    # Selecting the next sector flushes the complete first sector. ABORT then
    # discards the guard sector without entering manifestation.
    session.dev.ctrl_transfer(
        0x21, dfu.DFU_DNLOAD, blocks, 0, guard, timeout=5000)
    session.wait_download_idle(f"standard guard block {blocks}")
    session.abort()
    time.sleep(0.1)
    expect_idle(session, "ABORT returns to dfuIDLE")

    expect("standard Upload readback",
           upload(session, base, len(image), standard=True) == image)
    expect("DfuSe Upload readback",
           session.upload_bytes(base, len(image)) == image)

    session.erase(base)
    session.erase(base + dfu.ERASE_SIZE)
    erased = session.upload_bytes(base, 32)
    expect("test sectors restored to erased state",
           erased == bytes((0x39, 0xE3)) * 16)
    expect_idle(session, "final dfuIDLE")


def main():
    parser = argparse.ArgumentParser(
        description="Run the release-safe 16-KB layout smoke test")
    parser.parse_args()

    session = DfuSession()
    session.open()
    try:
        print(f"DFU {session.dev.idVendor:04X}:{session.dev.idProduct:04X} "
              f"speed={session.dev.speed}")
        test_descriptor(session)
        test_boundary(session)
        test_standard_download_upload(session)
    finally:
        session.close()
    print("\n16-KB LAYOUT TEST COMPLETE")


if __name__ == "__main__":
    main()
