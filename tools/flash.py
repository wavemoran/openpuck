#!/usr/bin/env python3
"""Back up, flash and restore a Life Puck over its USB-C port.

The puck's power is held on by firmware (GPIO7), so a plain esptool reset can
switch it off. This tool connects, then holds the power latch from the ROM
loader. If the running firmware lets the power drop during the reset (factory
firmware does), it asks you to hold the power button while it reconnects.

Run inside an ESP-IDF environment (it provides esptool and pyserial):
  python tools/flash.py info
  python tools/flash.py backup my-puck-backup.bin
  python tools/flash.py flash --backup my-puck-backup.bin
  python tools/flash.py restore my-puck-backup.bin
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time

import serial.tools.list_ports
from esptool.cmds import (attach_flash, detect_chip, erase_region, read_flash,
                          reset_chip, run_stub, verify_flash, write_flash)

USB_VID, USB_PID = 0x303A, 0x1001     # ESP32-C3 built-in USB-Serial-JTAG
FLASH_SIZE = 4 * 1024 * 1024
LATCH_PIN, BUTTON_PIN = 7, 10

IO_MUX_GPIO0 = 0x60009004             # IO_MUX_GPIOn_REG = this + 4*n
GPIO_OUT_W1TS = 0x60004008
GPIO_ENABLE_W1TS = 0x60004024
GPIO_IN = 0x6000403C
GPIO_FUNC0_OUT_SEL = 0x60004554       # + 4*n
MCU_SEL_GPIO, FUN_IE = 1 << 12, 1 << 9
OUT_SEL_SIMPLE_GPIO = 0x80

PARTITION_TABLE_OFFSET = 0x8000
DEFAULT_BUILD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")


def say(text):
    print(f">>> {text}", flush=True)
    if shutil.which("say"):
        subprocess.Popen(["say", text])


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == USB_VID and p.pid == USB_PID:
            return p.device
    return None


def wait_for_port(present, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if bool(find_port()) == present:
            return find_port()
        time.sleep(0.1)
    return None


def as_gpio(esp, pin, input_enable):
    reg = IO_MUX_GPIO0 + 4 * pin
    value = (esp.read_reg(reg) & ~(0x7 << 12)) | MCU_SEL_GPIO
    esp.write_reg(reg, value | FUN_IE if input_enable else value)


def hold_power(esp):
    """Drive the power latch high from the ROM loader, then wait for the button
    to be released if it is being held."""
    as_gpio(esp, LATCH_PIN, input_enable=False)
    esp.write_reg(GPIO_FUNC0_OUT_SEL + 4 * LATCH_PIN, OUT_SEL_SIMPLE_GPIO)
    esp.write_reg(GPIO_OUT_W1TS, 1 << LATCH_PIN)
    esp.write_reg(GPIO_ENABLE_W1TS, 1 << LATCH_PIN)
    as_gpio(esp, BUTTON_PIN, input_enable=True)
    if (esp.read_reg(GPIO_IN) >> BUTTON_PIN) & 1:
        say("Release the button now")
        end = time.time() + 30
        while (esp.read_reg(GPIO_IN) >> BUTTON_PIN) & 1:
            if time.time() > end:
                sys.exit("Button still held after 30 s; giving up.")
            time.sleep(0.1)
        time.sleep(0.5)


def try_connect(port):
    try:
        return detect_chip(port, 115200, "default-reset", connect_attempts=3)
    except Exception as e:
        print(f"Connection failed: {e}")
        return None


def connect():
    port = find_port()
    if port:
        print(f"Found the puck on {port}; connecting.")
        esp = try_connect(port)
        if esp:
            hold_power(esp)
            return esp
        print("The puck switched off during the reset (factory firmware does this).")
        wait_for_port(False, 5)

    if port:
        # It was on but switched off during the reset: factory firmware.
        say("Press and hold the power button")
        print("Keep holding until told to release. Waiting up to 10 minutes...")
    else:
        say("Turn on the puck")
        print("Press the power button. If it still has its factory firmware, keep\n"
              "holding until told to release. Waiting up to 10 minutes...")
    port = wait_for_port(True, 600)
    if not port:
        sys.exit("The puck never appeared on USB.")
    time.sleep(2)
    esp = try_connect(port)
    if not esp:
        sys.exit("Could not connect even with the button held.")
    hold_power(esp)
    return esp


def flash_size_from_id(flash_id):
    capacity = (flash_id >> 16) & 0xFF
    return 1 << capacity if capacity < 32 else None


def is_compatible_4mb_flash(flash_id):
    capacity = (flash_id >> 16) & 0xFF
    flash_type = (flash_id >> 8) & 0xFF
    manufacturer = flash_id & 0xFF
    return (capacity == 0x16 and flash_type != 0xFF and
            manufacturer not in (0x00, 0xFF))


def prepare(esp, require_4mb):
    print(f"Connected: {esp.get_chip_description()}, "
          f"MAC {':'.join(f'{b:02x}' for b in esp.read_mac())}")
    esp = run_stub(esp)
    attach_flash(esp)
    flash_id = esp.flash_id()
    size = flash_size_from_id(flash_id)
    if size is None:
        print(f"Flash ID {flash_id:#08x}")
    else:
        print(f"Flash ID {flash_id:#08x}, {size // 1024} KB")
    if require_4mb and not is_compatible_4mb_flash(flash_id):
        sys.exit(f"Unexpected flash chip (ID {flash_id:#08x}); refusing to write.")
    return esp, size


def check_image(path):
    if not os.path.isfile(path):
        sys.exit(f"{path} not found. Make a backup first: python tools/flash.py backup <file>")
    data = open(path, "rb").read()
    problems = []
    if len(data) != FLASH_SIZE:
        problems.append(f"size is {len(data)} bytes, expected {FLASH_SIZE}")
    elif data[0] != 0xE9:
        problems.append("no bootloader image at offset 0")
    elif data[PARTITION_TABLE_OFFSET:PARTITION_TABLE_OFFSET + 2] != b"\xaa\x50":
        problems.append("no partition table at 0x8000")
    if problems:
        sys.exit(f"{path} doesn't look like a full flash backup: {'; '.join(problems)}")
    return data


def data_partitions(table):
    """(offset, size) of every data partition in a partition table image."""
    out = []
    for i in range(0, len(table), 32):
        entry = table[i:i + 32]
        if entry[:2] != b"\xaa\x50":
            break
        if entry[2] == 1:
            out.append((int.from_bytes(entry[4:8], "little"), int.from_bytes(entry[8:12], "little")))
    return out


def build_images(build_dir):
    args_file = os.path.join(build_dir, "flash_args")
    if not os.path.isfile(args_file):
        sys.exit(f"No build found in {build_dir}; run `idf.py build` first.")
    images = []
    for line in open(args_file).read().splitlines()[1:]:
        if line.strip():
            offset, rel = line.split()
            images.append((int(offset, 0), os.path.join(build_dir, rel)))
    return images


def finish(esp):
    reset_chip(esp, "hard-reset")
    say("Done")
    print("The puck has been reset. If it stays off, press the power button.")


def cmd_info(args):
    esp, _ = prepare(connect(), require_4mb=False)
    finish(esp)


def cmd_backup(args):
    if os.path.exists(args.output):
        sys.exit(f"{args.output} already exists; choose another name.")
    esp, size = prepare(connect(), require_4mb=False)
    if size is None:
        finish(esp)
        sys.exit("Could not determine the flash size; backup was not created.")
    print(f"Reading {size // 1024} KB; this takes about a minute.")
    read_flash(esp, 0, size, output=args.output)
    digest = hashlib.sha256(open(args.output, "rb").read()).hexdigest()
    with open(args.output + ".sha256", "w") as f:
        f.write(f"{digest}  {os.path.basename(args.output)}\n")
    print(f"Saved {args.output} (SHA-256 {digest})")
    finish(esp)


def cmd_flash(args):
    check_image(args.backup)
    images = build_images(args.build)
    new_table = open(dict((o, p) for o, p in images)[PARTITION_TABLE_OFFSET], "rb").read()
    esp, _ = prepare(connect(), require_4mb=True)

    current_table = read_flash(esp, PARTITION_TABLE_OFFSET, 0xC00, no_progress=True)
    if current_table != new_table[:0xC00]:
        # A different layout leaves stale data where our data partitions now live.
        for offset, size in data_partitions(new_table):
            print(f"Partition layout changed: erasing data partition at {offset:#x}")
            erase_region(esp, offset, size)
    write_flash(esp, images)
    verify_flash(esp, images)
    finish(esp)


def cmd_restore(args):
    check_image(args.image)
    esp, _ = prepare(connect(), require_4mb=True)
    images = [(0, args.image)]
    write_flash(esp, images)
    verify_flash(esp, images)
    finish(esp)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("info", help="connect and show chip and flash details (read-only)")
    p = sub.add_parser("backup", help="save the whole flash to a file")
    p.add_argument("output")
    p = sub.add_parser("flash", help="write the OpenPuck build")
    p.add_argument("--backup", required=True, help="your full flash backup (required)")
    p.add_argument("--build", default=DEFAULT_BUILD_DIR, help="ESP-IDF build directory")
    p = sub.add_parser("restore", help="write a full flash backup back to the puck")
    p.add_argument("image")
    args = parser.parse_args()
    {"info": cmd_info, "backup": cmd_backup, "flash": cmd_flash, "restore": cmd_restore}[args.command](args)


if __name__ == "__main__":
    main()
