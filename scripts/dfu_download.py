#!/usr/bin/env python3
"""CH32H417 DFU/DfuSe test utility.

Examples:
  python dfu_download.py app.bin
  python dfu_download.py app.bin --addr 0x08020000 --erase --exec 0x08020000
  python dfu_download.py app.bin --standard
  python dfu_download.py --upload readback.bin --addr 0x08004000 --length 4096
"""

import argparse
import struct
import sys
import time

import usb.core
import usb.backend.libusb1
import usb.util
try:
    import libusb_package
except ImportError:
    libusb_package = None

VID = 0x0483
PID = 0xDF11
TRANSFER_SIZE = 512
ERASE_SIZE = 0x2000
APP_BASE = 0x08004000

DFU_DETACH = 0
DFU_DNLOAD = 1
DFU_UPLOAD = 2
DFU_GETSTATUS = 3
DFU_CLRSTATUS = 4
DFU_GETSTATE = 5
DFU_ABORT = 6

DFU_IDLE = 2
DFU_DNLOAD_IDLE = 5
DFU_UPLOAD_IDLE = 9
DFU_ERROR = 10

CMD_SET_ADDRESS = 0x21
CMD_ERASE = 0x41
CMD_SET_EXEC_ADDRESS = 0x51


def find_device():
    backend = None
    if libusb_package is not None:
        backend = usb.backend.libusb1.get_backend(
            find_library=libusb_package.find_library)
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
    if dev is None:
        raise RuntimeError("DFU device 0483:df11 not found")
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass
    return dev


def get_status(dev):
    data = bytes(dev.ctrl_transfer(0xA1, DFU_GETSTATUS, 0, 0, 6, timeout=5000))
    if len(data) != 6:
        raise RuntimeError("short GETSTATUS response")
    timeout_ms = data[1] | (data[2] << 8) | (data[3] << 16)
    return data[0], timeout_ms, data[4]


def wait_download_idle(dev):
    for _ in range(200):
        status, timeout_ms, state = get_status(dev)
        if status != 0 or state == DFU_ERROR:
            raise RuntimeError(f"DFU error: status=0x{status:02x}, state={state}")
        if state == DFU_DNLOAD_IDLE:
            return
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError("timeout waiting for dfuDNLOAD-IDLE")


def send_command(dev, command, address):
    payload = struct.pack("<BI", command, address)
    dev.ctrl_transfer(0x21, DFU_DNLOAD, 0, 0, payload, timeout=5000)
    wait_download_idle(dev)


def erase_range(dev, address, length):
    first = address & ~(ERASE_SIZE - 1)
    last = (address + length + ERASE_SIZE - 1) & ~(ERASE_SIZE - 1)
    for sector in range(first, last, ERASE_SIZE):
        print(f"  erase 0x{sector:08X}")
        send_command(dev, CMD_ERASE, sector)


def download(dev, firmware, address, use_standard, erase, exec_address):
    with open(firmware, "rb") as stream:
        image = stream.read()
    if not image:
        raise RuntimeError("firmware file is empty")

    if use_standard:
        if address != APP_BASE:
            raise RuntimeError(f"standard DFU always downloads at 0x{APP_BASE:08X}")
        first_block = 0
    else:
        if erase:
            erase_range(dev, address, len(image))
        send_command(dev, CMD_SET_ADDRESS, address)
        first_block = 2

    blocks = (len(image) + TRANSFER_SIZE - 1) // TRANSFER_SIZE
    print(f"download {len(image)} bytes to 0x{address:08X} ({blocks} blocks)")
    for index in range(blocks):
        chunk = image[index * TRANSFER_SIZE:(index + 1) * TRANSFER_SIZE]
        dev.ctrl_transfer(0x21, DFU_DNLOAD, first_block + index, 0,
                          chunk, timeout=5000)
        wait_download_idle(dev)
        if index == 0 or (index + 1) % 32 == 0 or index + 1 == blocks:
            print(f"  {index + 1}/{blocks}")

    if exec_address is not None:
        send_command(dev, CMD_SET_EXEC_ADDRESS, exec_address)

    dev.ctrl_transfer(0x21, DFU_DNLOAD, 0, 0, b"", timeout=5000)
    for _ in range(200):
        try:
            status, timeout_ms, state = get_status(dev)
        except usb.core.USBError:
            return
        if status != 0:
            raise RuntimeError(f"manifest error: status=0x{status:02x}")
        if state == 8:
            return
        time.sleep(max(timeout_ms, 1) / 1000.0)
    raise RuntimeError("manifest timeout")


def upload(dev, output, address, length, use_standard):
    if use_standard:
        if address != APP_BASE:
            raise RuntimeError(f"standard DFU upload starts at 0x{APP_BASE:08X}")
        first_block = 0
    else:
        send_command(dev, CMD_SET_ADDRESS, address)
        first_block = 2

    result = bytearray()
    block = first_block
    while len(result) < length:
        request = min(TRANSFER_SIZE, length - len(result))
        chunk = bytes(dev.ctrl_transfer(0xA1, DFU_UPLOAD, block, 0,
                                        request, timeout=5000))
        result.extend(chunk)
        if len(chunk) < request:
            break
        block += 1
    try:
        dev.ctrl_transfer(0x21, DFU_ABORT, 0, 0, b"", timeout=1000)
    except usb.core.USBError:
        pass
    with open(output, "wb") as stream:
        stream.write(result)
    print(f"uploaded {len(result)} bytes from 0x{address:08X} to {output}")


def parse_int(value):
    return int(value, 0)


def main():
    parser = argparse.ArgumentParser(description="CH32H417 DFU/DfuSe utility")
    parser.add_argument("firmware", nargs="?", help="binary image to download")
    parser.add_argument("-a", "--addr", type=parse_int, default=APP_BASE)
    parser.add_argument("--standard", action="store_true",
                        help="use standard DFU block addressing")
    parser.add_argument("--erase", action="store_true",
                        help="explicitly erase all destination sectors first")
    parser.add_argument("--exec", dest="exec_address", type=parse_int,
                        help="set physical flash address used for the final jump")
    parser.add_argument("--upload", metavar="FILE", help="upload flash to FILE")
    parser.add_argument("--length", type=parse_int, default=4096,
                        help="upload length (default: 4096)")
    args = parser.parse_args()

    if bool(args.firmware) == bool(args.upload):
        parser.error("specify either firmware or --upload FILE")

    try:
        device = find_device()
        if args.upload:
            upload(device, args.upload, args.addr, args.length, args.standard)
        else:
            download(device, args.firmware, args.addr, args.standard,
                     args.erase, args.exec_address)
    except (RuntimeError, usb.core.USBError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
