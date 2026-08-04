#!/usr/bin/env python3
"""Two-stage manual power-interruption test for T18.

Run ``interrupt`` and remove board power while blocks are advancing. Restore
power with the switch in DFU mode, then run ``recover``.
"""

import argparse
import struct
import sys
import time

import usb.core
import usb.util

import dfu_download as dfu
from dfu_test_support import (
    SAFE_EXEC,
    DfuSession,
    download,
    erase_range,
    expect,
    make_pattern,
    upload,
)


START = 0x08010000
INTERRUPT_LENGTH = 0x000B0000


def wait_download_idle(dev):
    deadline = time.time() + 30.0
    while time.time() < deadline:
        status, timeout_ms, state = dfu.get_status(dev)
        if status != 0 or state == dfu.DFU_ERROR:
            raise RuntimeError(
                f"DFU error status=0x{status:02X}, state={state}")
        if state == dfu.DFU_DNLOAD_IDLE:
            return
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError("timeout waiting for download idle")


def interrupt_phase(delay_s=0.10, length=INTERRUPT_LENGTH):
    dev = dfu.find_device()
    completed = 0
    total = length // dfu.TRANSFER_SIZE
    print("T18 interrupt phase started", flush=True)
    try:
        try:
            dev.ctrl_transfer(0x21, dfu.DFU_ABORT, 0, 0, b"", timeout=2000)
        except usb.core.USBError:
            pass
        for command_id, address in ((dfu.CMD_ERASE, SAFE_EXEC),
                                    (dfu.CMD_SET_ADDRESS, START)):
            payload = struct.pack("<BI", command_id, address)
            dev.ctrl_transfer(0x21, dfu.DFU_DNLOAD, 0, 0,
                              payload, timeout=5000)
            wait_download_idle(dev)
            if command_id == dfu.CMD_ERASE:
                dev.ctrl_transfer(0x21, dfu.DFU_ABORT, 0, 0,
                                  b"", timeout=2000)
        print("POWER OFF NOW", flush=True)
        for block_index in range(total):
            seed = (block_index * 17 + 0x5A) & 0xFF
            chunk = bytes((seed + offset) & 0xFF
                          for offset in range(dfu.TRANSFER_SIZE))
            dev.ctrl_transfer(
                0x21, dfu.DFU_DNLOAD, block_index + 2, 0,
                chunk, timeout=5000)
            wait_download_idle(dev)
            completed = block_index + 1
            if completed == 1 or completed % 16 == 0:
                print(f"  blocks {completed}/{total}", flush=True)
            time.sleep(delay_s)
    except (usb.core.USBError, RuntimeError) as exc:
        if completed == 0:
            raise
        print(f"EXPECTED DISCONNECT after {completed}/{total} blocks: {exc}")
        print("Interrupt phase PASS; restore power in DFU mode and run recover")
        return
    finally:
        usb.util.dispose_resources(dev)

    raise RuntimeError("download completed without a power interruption")


def recover_phase():
    session = DfuSession()
    session.open()
    try:
        status, _, state = session.status()
        expect("bootloader enumerates in dfuIDLE after power restore",
               status == 0 and state == dfu.DFU_IDLE)
        session.erase(SAFE_EXEC)
        session.abort()

        image = make_pattern(dfu.ERASE_SIZE, 83)
        download(session, START, image, erase_first=True)
        expect("clean post-interruption download/readback",
               upload(session, START, len(image)) == image)

        erase_range(session, START, len(image))
        session.abort()
        print("T18 recovery phase PASS")
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("phase", choices=("interrupt", "recover"))
    parser.add_argument("--delay", type=float, default=0.10,
                        help="seconds to wait after every 512-byte block in interrupt phase")
    parser.add_argument("--length", type=lambda value: int(value, 0),
                        default=INTERRUPT_LENGTH,
                        help="bytes to stream during interrupt phase")
    args = parser.parse_args()
    if args.phase == "interrupt":
        interrupt_phase(args.delay, args.length)
    else:
        recover_phase()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
