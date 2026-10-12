# Reverse-engineering notes

How the protocol in [PROTOCOL.md](PROTOCOL.md) was recovered, where the evidence lives, and the
tricks that saved time. Keep this updated as new pieces get decoded.

## Vendor material (not in git)

Everything from Yamaha/Steinberg lives in `vendor/` and is git-ignored, because it is proprietary.

```
vendor/downloads/                original downloads (English docs only)
  TOOLS_for_UR44_V222_Win/       setup.exe (Inno Setup 5.5.7), manuals, release notes, OSS licenses
  YSUSB_V2111_Win/               standalone driver V2.1.11 (Inno Setup 6.7, not extractable with innoextract 1.9)
  FirmwareUpdater_V212/          UR44_firmware_V212.exe + update guide/release notes
vendor/extracted/                built by re/extract_vendor.sh (SHA256SUMS inside)
  tools_v222/dspmixfx_x86/       dspMixFx GUI, memapi, Cubase extensions, Qt4 DLLs, factory scenes
  tools_v222/helpers_x64/        64-bit memapi + extensions
  tools_v222/ysusb_driver/{x86,x64}/   YSUSB driver bundled with TOOLS (sys/inf/cat, bulklib, ASIO, control panel)
  firmware_v212/                 upd_ur44_boot_main_v212.bin (+ the updater's icon)
vendor/analysis/ghidra/          full decompilations, built by re/ghidra/decompile_all.sh
```

Translations of the manuals and the eLicenser installer were deleted. They are irrelevant, and
can be re-downloaded from Steinberg if ever needed.

## Tooling

| Need | What worked |
|---|---|
| Inno Setup 5.x installer | `innoextract` (system package, or the Arch package unpacked into a temp dir when there is no root) |
| MSI payload | Don't bother with the .msi; `cabextract` the sibling `.cab` files. Names are mangled sometimes (`foo.dll1`, truncated preset names) |
| Inno 6.7 | Not supported by innoextract 1.9. binwalk "finds" CAB headers inside it, but they are inside Inno's compressed stream and fail checksums, so they are false positives |
| 7z on the installers | Extracts only the uncompressed stub resources, plus zero-length placeholders. Misleading |
| Decompilation | Ghidra 12.1 headless + `re/ghidra/DumpAll.java`; needs JDK 21 (`JAVA_HOME=/usr/lib/jvm/java-21-openjdk`). The REA MCP server only finds Ghidra if `GHIDRA_INSTALL_DIR=/opt/ghidra` and a JDK 21 are in *its* environment |
| Device I/O | ALSA rawmidi via ctypes (`tools/ur44.py`); `amidi -p hw:0,0,1 -S '…' -d -t 1` for one-offs |

## Binary roles (TOOLS V2.2.2)

* `dspmixfx_ur44.exe` – Qt 4 GUI. It holds hardly any protocol knowledge: it talks to memapi over COM.
* `memapi_ur44.dll` – COM "MemAPI UR44" 2.1.0. **This is the one to read.** It contains the
  parameter model, SysEx encoder/decoder, display-label tables and the reverb-time maths.
* `ysusb_bulklib{32,64}.dll` – COM transport (`IUsbBulk`, `IUsbBulkDev`, `IUsbBulkPort`):
  SetupDi + DeviceIoControl into the YSUSB driver. Under the vendor USB config (config 2) the device
  has an extra bulk pair (IF4 EP 0x03/0x84); this lib is how Windows reaches it.
* `ur44_extension.dll`, `ysusb_aadm_extension.dll` – Cubase plugins (`GetPluginFactory`).
* `UR44_firmware_V212.exe` – statically linked Qt updater, also using IUsbBulkPort.
* `steinberg_ur44_*` – factory scenes in Cubase's RIFF/`NUNDROOT` container. Not decoded yet.

## memapi_ur44.dll (32-bit) landmarks

Image base 0x10000000; section VA == file offset + base, so `va - 0x10000000` is the file offset.

| What | Where |
|---|---|
| SysEx headers `F0 43 {10,30,20} 3E 14` | `.rdata` 0x10019354 / 0x1001935c / 0x10019398 |
| Incoming SysEx parser | FUN_1000bc00 (u16/u32 7-bit decoders FUN_1000bcc0 / FUN_1000bd20) |
| Outgoing command dispatcher | FUN_10008d90 (switch on command kind 0..0xd) |
| Generic param set / get builders | FUN_10008fa0 / FUN_10009090. The other builders (FUN_10009170, 9330, 94b0, 96c0, 9870, 9a10, 9c10) are scene/dump/system commands, **not decoded yet** |
| 7-bit packers | FUN_100033b0 (u16 → 3 bytes), FUN_10003450 (u32 → 5 bytes) |
| Send path | FUN_1000a510 → vtable +0x10 of the IUsbBulkPort |
| Parameter descriptors | `PTR_DAT_10022118[0..0xb0]`; decoded by `re/gen_params.py` |
| Display labels | `PTR_PTR_10025778[id] -> {char **labels, char *unit}`, lookup FUN_1000ada0; decoded by `re/dump_memapi_labels.py` |
| Reverb time text | FUN_1000ae60 / FUN_1000b850 + 70 doubles at 0x10019120 |

## Tricks that paid off

* **Read the model DLL, not the GUI.** The GUI decompile (77k lines) was nearly useless. memapi
  (18k lines) had the whole protocol.
* **Grep the decompile for `0xf7`.** Every SysEx builder ends by pushing F7, which found all the
  message builders at once. The header constants were next to them.
* **The compiler merges duplicate strings, so look for pointer arrays.** `strings` on memapi showed
  "-74.00 … +6.00", "20 … 20k" but no tables. `re/find_string_ptr_tables.py` found the `char*`
  arrays, and the one function indexing the per-parameter table (FUN_1000ada0) gave the exact
  mapping. No curve fitting needed.
* **A float-table scan (monotonic runs of f32/f64) is mostly noise.** It is full of relocation and
  address junk. It only found the reverb-time doubles; the label approach was far better.
* **Probe read-only first.** Parameter requests (`F0 43 30 …`) are harmless. The device answered on
  MIDI port 2 only, and a `00 70 02` request returned the BOOT/MAIN version strings.
* **Match replies to requests.** A naive request → next-reply loop desyncs after one late reply,
  and produced a wrong id ↔ table "mismatch" for a while. Always compare the echoed (param, index).
* **The device doesn't bounds-check indices.** Out-of-range reads return neighbouring memory (e.g.
  0x040480C0), so a 2-D sweep needs the known ranges to be interpreted. Treat junk as "no instance".
* **Hardware-in-the-loop by ear.** Toggling `ChSendMixBus{1,2}Onf` and `PatchAuxPhoneOutput` while
  listening confirmed the write path. `monitor` while flipping +48V showed the unsolicited messages.
* **State persists on the device** (saved when [P] goes to standby). What you read is the last
  session's state, not factory defaults. Defaults come from memapi's descriptor table.

## Qt resource extraction (firmware updater)

The updater embeds its files as an rcc v1 bundle (`re/extract_qrc_firmware.py`):

* names table at file 0x4946a8: `u16 len, u32 hash, UTF-16BE name`. It holds `icon.png` and
  `upd_ur44_boot_main_v212.bin`.
* tree right after it: 14-byte BE entries. The firmware entry has flags=1, meaning zlib:
  `u32 size, u32 uncompressed size, zlib stream`.
* data section located via the length-prefixed PNG at data offset 0.

## Firmware image (first look)

`upd_ur44_boot_main_v212.bin`, 1,114,624 bytes:

* 0x00 `"MMPFUPDD"` `"V0.01"` magic/format version; 0x1c `"UR44"`; 0x2c `"BOOT"`; 0x5c `"V2.12 "`.
* 0x64–0xA3: 4 × 16-byte segment descriptors. Not decoded; contains `…6400 6400…` and sizes like
  0x4000 / 0x2000 / 0x8000.
* 0xA4: ARM exception vector table (`e59ff014` = `ldr pc,[pc,#0x14]`), with words stored big-endian.
* The upload protocol is not known yet. It goes through IUsbBulkPort, so it probably uses the vendor
  config's bulk endpoints, or SysEx; to be found in the updater decompile (`vendor/analysis/ghidra/fwupd.c`).

## Open items

* Scene store/recall, "Initialize All", dump request / bulk dump format, the text-payload command.
  Decode them before sending: they can overwrite stored scenes.
* Level meters (which message carries them).
* `FxRevTime` exact formula; factory scene file format.
* Firmware update protocol, for a cross-platform updater.
