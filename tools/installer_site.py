#!/usr/bin/env python3
"""Assemble the browser installer site (installer/ plus the firmware to flash).

Copies installer/ (minus test/ and any firmware/) into the output directory and
writes firmware/manifest.json and the three images next to it. The firmware
comes from a local build or from a release's openpuck-<version>-parts.zip.

  python3 tools/installer_site.py --build-dir build --version dev
  python3 tools/installer_site.py --parts-zip openpuck-0.1.0-parts.zip --version 0.1.0
  python3 -m http.server -d build-installer 8000
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SITE_DIR = os.path.join(ROOT, "installer")
DEFAULT_OUT = "build-installer"

# Expected images: file name -> (offset, required leading bytes)
IMAGES = {
    "bootloader.bin": (0x0, b"\xe9"),
    "partition-table.bin": (0x8000, b"\xaa\x50"),
    "openpuck.bin": (0x10000, b"\xe9"),
}
ZIP_MEMBERS = ("flash_args",) + tuple(IMAGES)


def die(msg):
    sys.exit(f"error: {msg}")


def parse_flash_args(text):
    """Return {file name: (offset, path as written)} from flash_args text."""
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    found = {}
    for ln in lines[1:]:              # first line holds the esptool options
        fields = ln.split()
        if len(fields) != 2:
            die(f"unexpected flash_args line: {ln!r}")
        try:
            offset = int(fields[0], 0)
        except ValueError:
            die(f"bad offset in flash_args line: {ln!r}")
        name = fields[1].replace("\\", "/").rsplit("/", 1)[-1]
        if name in found:
            die(f"flash_args lists {name} twice")
        found[name] = (offset, fields[1])
    return found


def check_layout(entries):
    """Require exactly the three images at their fixed offsets."""
    extra = sorted(set(entries) - set(IMAGES))
    missing = sorted(set(IMAGES) - set(entries))
    if missing:
        die("flash_args is missing: " + ", ".join(missing))
    if extra:
        die("flash_args lists unexpected images: " + ", ".join(extra))
    for name, (want, _) in IMAGES.items():
        got = entries[name][0]
        if got != want:
            die(f"{name} is at {got:#x} in flash_args, expected {want:#x}")


def check_magic(name, data):
    magic = IMAGES[name][1]
    if not data.startswith(magic):
        die(f"{name} does not start with {magic.hex(' ')} "
            f"(starts with {data[:len(magic)].hex(' ') or 'nothing'})")


def load_from_build_dir(build_dir):
    flash_args = os.path.join(build_dir, "flash_args")
    if not os.path.isfile(flash_args):
        die(f"{flash_args} not found (build first)")
    with open(flash_args, encoding="utf-8") as f:
        entries = parse_flash_args(f.read())
    check_layout(entries)
    images = {}
    for name, (_, rel) in entries.items():
        path = os.path.normpath(os.path.join(build_dir, rel))
        if not os.path.isfile(path):
            die(f"{path} not found")
        with open(path, "rb") as f:
            images[name] = f.read()
    return images


def load_from_zip(zip_path):
    if not os.path.isfile(zip_path):
        die(f"{zip_path} not found")
    try:
        zf = zipfile.ZipFile(zip_path)
    except zipfile.BadZipFile:
        die(f"{zip_path} is not a zip file")
    with zf:
        names = zf.namelist()
        for n in names:
            if n.endswith("/"):
                continue
            if "/" in n or "\\" in n or n in ("", ".", ".."):
                die(f"zip member {n!r} has a path separator; refusing")
            if n not in ZIP_MEMBERS:
                die(f"unexpected zip member {n!r}")
        missing = [n for n in ZIP_MEMBERS if n not in names]
        if missing:
            die("zip is missing: " + ", ".join(missing))
        entries = parse_flash_args(zf.read("flash_args").decode("utf-8"))
        check_layout(entries)
        return {name: zf.read(name) for name in IMAGES}


def prepare_out(out, force):
    if os.path.lexists(out):
        if not os.path.isdir(out) or os.path.islink(out):
            die(f"{out} exists and is not a directory")
        if os.listdir(out):
            if not force:
                die(f"{out} is not empty (use --force to replace a previous output)")
            if not os.path.isfile(os.path.join(out, "firmware", "manifest.json")):
                die(f"{out} does not look like a previous installer output "
                    "(no firmware/manifest.json); refusing to delete it")
            shutil.rmtree(out)
    os.makedirs(out, exist_ok=True)


def copy_site(out):
    if not os.path.isdir(SITE_DIR):
        die(f"{os.path.normpath(SITE_DIR)} not found")
    skip = {os.path.join(SITE_DIR, "test"), os.path.join(SITE_DIR, "firmware")}

    def ignore(directory, names):
        return [n for n in names if os.path.join(directory, n) in skip]

    shutil.copytree(SITE_DIR, out, ignore=ignore, dirs_exist_ok=True)


def write_firmware(out, images, version):
    fw_dir = os.path.join(out, "firmware")
    os.makedirs(fw_dir, exist_ok=True)
    parts = []
    for name, data in images.items():
        with open(os.path.join(fw_dir, name), "wb") as f:
            f.write(data)
        parts.append({"offset": IMAGES[name][0], "path": name, "size": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()})
    parts.sort(key=lambda p: p["offset"])
    manifest = {"name": "OpenPuck", "version": version, "chip": "ESP32-C3",
                "parts": parts}
    with open(os.path.join(fw_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    return parts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--build-dir", metavar="DIR",
                     help="ESP-IDF build directory containing flash_args (e.g. build)")
    src.add_argument("--parts-zip", metavar="FILE",
                     help="release openpuck-<version>-parts.zip")
    ap.add_argument("--version", required=True,
                    help="version string shown by the installer (use dev for local builds)")
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help=f"output directory (default {DEFAULT_OUT})")
    ap.add_argument("--force", action="store_true",
                    help="replace an existing previous output directory")
    args = ap.parse_args()

    if args.build_dir:
        images = load_from_build_dir(args.build_dir)
    else:
        images = load_from_zip(args.parts_zip)
    for name, data in images.items():
        check_magic(name, data)

    prepare_out(args.out, args.force)
    copy_site(args.out)
    parts = write_firmware(args.out, images, args.version)

    print(f"OpenPuck installer {args.version}")
    for p in parts:
        print(f"  {p['offset']:#08x}  {p['path']:<20} {p['size']:>8} bytes  {p['sha256']}")
    print(f"Site written to {os.path.abspath(args.out)}")
    print(f"Try it: python3 -m http.server -d {args.out} 8000, "
          "then open http://localhost:8000/")


if __name__ == "__main__":
    main()
