#!/usr/bin/env python3
"""Shared host-side helpers for CH32H417 DFU hardware tests."""

import time

import usb.core
import usb.util

import dfu_download as dfu


APP_END = 0x080F0000
SAFE_EXEC = 0x080D0000
ERR_ADDRESS = 0x08
ERR_NOTDONE = 0x09
ERASED_32 = bytes((0x39, 0xE3)) * 16


def make_pattern(length, seed, entry=True):
    data = bytearray(((index * seed + (seed ^ 0x5A)) & 0xFF)
                     for index in range(length))
    if entry and length >= 4:
        data[0:4] = b"\xef\xbe\xad\xde"
    return bytes(data)


def expect(name, condition):
    if not condition:
        raise RuntimeError(f"{name}: FAIL")
    print(f"{name}: PASS")


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
                    raise RuntimeError(
                        f"DFU device unavailable: {last}") from exc
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
        while time.time() < deadline:
            status, timeout_ms, state = self.status()
            current = (status, state, timeout_ms)
            if current != last:
                print(f"    {label}: status=0x{status:02X} state={state} "
                      f"timeout={timeout_ms}ms")
                last = current
            if status != 0 or state == dfu.DFU_ERROR:
                raise RuntimeError(
                    f"{label}: DFU error status=0x{status:02X}, state={state}")
            if state == dfu.DFU_DNLOAD_IDLE:
                return
            time.sleep(max(timeout_ms, 1) / 1000.0)
        raise RuntimeError(f"{label}: timeout waiting for dfuDNLOAD-IDLE")

    def clear_status(self):
        self.dev.ctrl_transfer(
            0x21, dfu.DFU_CLRSTATUS, 0, 0, b"", timeout=2000)

    def abort(self):
        try:
            self.dev.ctrl_transfer(
                0x21, dfu.DFU_ABORT, 0, 0, b"", timeout=2000)
        except usb.core.USBError:
            pass

    def command_raw(self, command, address):
        payload = bytes((command,)) + address.to_bytes(4, "little")
        self.dev.ctrl_transfer(
            0x21, dfu.DFU_DNLOAD, 0, 0, payload, timeout=5000)

    def command(self, command, address):
        self.command_raw(command, address)
        self.wait_download_idle(f"cmd 0x{command:02X} @ 0x{address:08X}")

    def erase(self, address):
        self.command(dfu.CMD_ERASE, address)

    def set_address(self, address):
        self.command(dfu.CMD_SET_ADDRESS, address)

    def upload_bytes(self, address, length):
        return upload(self, address, length)


def expect_idle(session, label):
    status, _, state = session.status()
    expect(label, status == 0 and state == dfu.DFU_IDLE)


def erase_range(session, address, length):
    first = address & ~(dfu.ERASE_SIZE - 1)
    last = (address + length + dfu.ERASE_SIZE - 1) & ~(dfu.ERASE_SIZE - 1)
    for sector in range(first, last, dfu.ERASE_SIZE):
        session.erase(sector)


def manifest_to_blank(session, exec_address=SAFE_EXEC):
    session.command(dfu.CMD_SET_EXEC_ADDRESS, exec_address)
    session.dev.ctrl_transfer(
        0x21, dfu.DFU_DNLOAD, 0, 0, b"", timeout=5000)
    deadline = time.time() + 30.0
    last = None
    while time.time() < deadline:
        status, timeout_ms, state = session.status()
        current = (status, state, timeout_ms)
        if current != last:
            print(f"    manifest: status=0x{status:02X} state={state} "
                  f"timeout={timeout_ms}ms")
            last = current
        if status != 0 or state == dfu.DFU_ERROR:
            raise RuntimeError(
                f"manifest failed: status=0x{status:02X}, state={state}")
        if state == dfu.DFU_IDLE:
            return
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError("timeout waiting for safe manifestation")


def download(session, address, data, standard=False, erase_first=False):
    if erase_first:
        erase_range(session, address, len(data))
    if not standard:
        session.set_address(address)
    first_block = 0 if standard else 2
    blocks = (len(data) + dfu.TRANSFER_SIZE - 1) // dfu.TRANSFER_SIZE
    for index in range(blocks):
        chunk = data[index * dfu.TRANSFER_SIZE:
                     (index + 1) * dfu.TRANSFER_SIZE]
        block = first_block + index
        session.dev.ctrl_transfer(
            0x21, dfu.DFU_DNLOAD, block, 0, chunk, timeout=5000)
        session.wait_download_idle(f"block {block}")
    manifest_to_blank(session)


def upload(session, address, length, standard=False):
    if not standard:
        session.set_address(address)
    first_block = 0 if standard else 2
    result = bytearray()
    block = first_block
    while len(result) < length:
        request = min(dfu.TRANSFER_SIZE, length - len(result))
        chunk = bytes(session.dev.ctrl_transfer(
            0xA1, dfu.DFU_UPLOAD, block, 0, request, timeout=5000))
        result.extend(chunk)
        if len(chunk) < request:
            break
        block += 1
    session.abort()
    return bytes(result)
