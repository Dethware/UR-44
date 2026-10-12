#!/usr/bin/env python3
"""Download the Yamaha/Steinberg UR44 software into vendor/downloads/ and verify it.

Sources:
  https://o.steinberg.net/en/support/downloads_hardware/downloads_ur44.html           (TOOLS, firmware)
  https://o.steinberg.net/en/support/downloads_hardware/yamaha_steinberg_usb_driver.html (driver <= 2.1.7)
  https://asia-oceania.yamaha.com/en/support/updates/yamaha-steinberg-usb-driver-for-win.html (driver 2.1.11)

Zips are cached in vendor/cache/ and unpacked into vendor/downloads/<dest>/ (a single top-level
directory in the zip is stripped). Non-English PDF translations and macOS "__MACOSX" metadata are dropped.

Yamaha's site shows a license agreement before its download button. Items marked `license`
are only fetched with --accept-yamaha-license, meaning you have read and accepted that license.

Usage: download_vendor.py [--group core|extra|all] [--only TEXT] [--accept-yamaha-license] [--force]
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
import urllib.request
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CACHE = os.path.join(ROOT, "vendor", "cache")
DOWNLOADS = os.path.join(ROOT, "vendor", "downloads")
UA = "Mozilla/5.0 (X11; Linux x86_64) ur44-re/1.0"

SB = "https://download.steinberg.net/downloads_hardware"

# sha256: hash of the zip. inner: {path inside dest: sha256} checked after unpacking.
# exclude: path prefixes (inside dest) that are not unpacked.
# A None hash is trust-on-first-use: the script prints the hash so it can be pinned here.
MANIFEST = [
    dict(name="TOOLS for UR44 V2.2.2 (Windows)", group="core",
         url=f"{SB}/UR44/PC/2.2.2/TOOLS_for_UR44_V222_Win.zip", dest="TOOLS_for_UR44_V222_Win",
         sha256="66b20f01da8c2ae77849285169ded76428404ffc028453686c80a33b9fc65588",
         inner={"setup.exe": "9947569b8c78fc6d4f22a0e14f6af3d9221664ce03c037aff56584b115b7afd5"},
         exclude=["eLicenser/"]),  # Steinberg copy protection for Cubase AI; irrelevant
    dict(name="UR44 Firmware Updater V2.12 (Windows)", group="core",
         url=f"{SB}/UR44/PC/UR44_Firmware_Updater_V212_win.zip", dest="FirmwareUpdater_V212",
         sha256="34f43530810298af62f9622b1480c8069440b2745a18c78cc626ce5173134553",
         inner={"UR44_firmware_V212.exe": "6e4929e90b740710ff3e2c1eea6dd1024d418ef0714bb6b32ba5f5961aeb6018"}),
    dict(name="Yamaha Steinberg USB Driver V2.1.11 (Windows)", group="core", license=True,
         url="https://asia-oceania.yamaha.com/en/download/files/3232383/", dest="YSUSB_V2111_Win",
         license_page="https://asia-oceania.yamaha.com/en/support/updates/yamaha-steinberg-usb-driver-for-win.html",
         sha256="f94676d93bcc29d612a09c6b1909a3144c1bf761ec42a71ad1f479605df7ff9b",
         inner={"Install_Yamaha_Steinberg_USB_Driver.exe":
                "5103faceb00200b5c1db60cbf5a45a68bebd50fee4eb1c83ed39fa043ed30d4c"}),
    dict(name="Yamaha Steinberg USB Driver V2.1.7 (Windows, Inno 6.1 - extractable)", group="extra",
         url=f"{SB}/Yamaha_Steinberg_USB_Driver/Win/2.1.7/YSUSB_V217_Win.zip", dest="YSUSB_V217_Win",
         sha256="dcd0c05eb7006be957a2c60fc26b0940958a84063ec41f8f5364b667eae58c8e", inner={}),
    dict(name="UR44 Firmware Updater V2.12-2 (macOS)", group="extra",
         url=f"{SB}/UR44/Mac/UR44_Firmware_Updater_V212-2_mac.zip", dest="FirmwareUpdater_V212-2_mac",
         sha256="8244bce3dd5fdbe17e4a0f7ddab003bfc2ff608edece85d77b27e13b479025cb", inner={}),
    dict(name="TOOLS for UR44 V2.3.0 (macOS, 670 MB)", group="extra",
         url=f"{SB}/UR44/Mac/2.3.0/TOOLS_for_UR44_V230_Mac.zip", dest="TOOLS_for_UR44_V230_Mac",
         sha256=None, inner={}),
]

TRANSLATION = re.compile(r"(_(de|es|fr|it|ja|jp|pt|zh|ko|ru)\.pdf$)|((^|/)(DE|ES|FR|IT|JA|JP|PT|ZH|KO|RU)_[^/]*\.pdf$)")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url, path):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    tmp = path + ".part"
    with urllib.request.urlopen(req) as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while chunk := r.read(1 << 20):
            f.write(chunk)
            done += len(chunk)
            if total and sys.stderr.isatty():
                print(f"\r  {done >> 20}/{total >> 20} MiB", end="", file=sys.stderr)
    if sys.stderr.isatty():
        print(file=sys.stderr)
    os.replace(tmp, path)


def unpack(zpath, dest, exclude=()):
    with zipfile.ZipFile(zpath) as z:
        names = [n for n in z.namelist() if not n.startswith("__MACOSX/") and not n.endswith("/")]
        # strip every leading directory shared by all entries (some zips nest it twice)
        prefix = ""
        while True:
            tops = {n[len(prefix):].split("/", 1)[0] for n in names}
            if len(tops) != 1 or not all("/" in n[len(prefix):] for n in names):
                break
            prefix += tops.pop() + "/"
        if os.path.exists(dest):
            shutil.rmtree(dest)
        kept = 0
        for n in names:
            rel = n[len(prefix):]
            if (TRANSLATION.search(rel) or os.path.basename(rel) == ".DS_Store"
                    or any(rel.startswith(e) for e in exclude)):
                continue
            out = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(n) as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
            mode = (z.getinfo(n).external_attr >> 16) & 0o777
            if mode & 0o111:
                os.chmod(out, mode)
            kept += 1
    return kept


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--group", choices=["core", "extra", "all"], default="core")
    ap.add_argument("--accept-yamaha-license", action="store_true")
    ap.add_argument("--force", action="store_true", help="re-download even if cached")
    ap.add_argument("--only", metavar="TEXT", help="only items whose name or dest contains TEXT")
    a = ap.parse_args()
    os.makedirs(CACHE, exist_ok=True)
    ok = True

    for item in MANIFEST:
        if a.only:
            if a.only.lower() not in (item["name"] + item["dest"]).lower():
                continue
        elif a.group != "all" and item["group"] != a.group:
            continue
        print(f"* {item['name']}")
        if item.get("license") and not a.accept_yamaha_license:
            print(f"  skipped: requires accepting Yamaha's license, see {item['license_page']}\n"
                  f"  re-run with --accept-yamaha-license once you have read and accepted it")
            continue
        zpath = os.path.join(CACHE, item["dest"] + ".zip")
        if a.force or not os.path.exists(zpath):
            fetch(item["url"], zpath)
        digest = sha256_file(zpath)
        if item["sha256"] is None:
            print(f"  unpinned zip sha256 {digest} (pin it in the manifest)")
        elif digest != item["sha256"]:
            print(f"  ERROR: sha256 mismatch: got {digest}, expected {item['sha256']}")
            ok = False
            continue
        dest = os.path.join(DOWNLOADS, item["dest"])
        n = unpack(zpath, dest, item.get("exclude", ()))
        for rel, want in item["inner"].items():
            path = os.path.join(dest, rel)
            if not os.path.exists(path):
                print(f"  ERROR: {rel} missing after unpacking")
                ok = False
                continue
            got = sha256_file(path)
            if got != want:
                print(f"  ERROR: {rel}: sha256 {got}, expected {want}")
                ok = False
        print(f"  -> vendor/downloads/{item['dest']}/ ({n} files)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
