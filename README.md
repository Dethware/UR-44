# UR-44

Linux (and eventually cross-platform) control of the **Steinberg UR44** USB audio interface's
internal DSP mixer: the settings dspMixFx manages on Windows/macOS. Those are input sends to MIX 1/2,
pan, DAW return, master, PHONES 2 source, loopback, REV-X, channel strip, guitar amp models,
LINE 5/6 level and sample rate.

On Linux the device runs in its class-compliant USB configuration. Audio works through
snd-usb-audio, and the mixer is controlled with Yamaha-style SysEx on the **second USB-MIDI port**.
No driver is needed.

## Layout

| Path | Contents |
|---|---|
| `tools/ur44.py` | CLI/library: `version`, `get`, `set`, `dump`, `mix 1\|2`, `phones2`, `monitor` |
| `tools/ur44_params.py`, `tools/ur44_labels.json` | Parameter table and display labels (generated from memapi) |
| `tools/ur44_sweet_spot.py` | The 34 factory Sweet Spot Data presets (Yamaha's, generated from the TOOLS .ssd files) |
| `gui/` | **UR44 Control**, the Qt 6 mixer application (C++17, CMake). See [gui/README](gui/README) for details |
| `docs/PROTOCOL.md` | The protocol as currently understood, with confirmed vs static-only marked |
| `docs/REVERSE_ENGINEERING.md` | Vendor binaries, addresses, tooling, tricks |
| `TODO.md` | Outstanding work and ideas |
| `docs/data/` | Raw evidence: descriptor dump, live address sweep, a state dump |
| `re/` | Reproducible RE scripts: vendor extraction, generators, Ghidra runner, finders, sweep |
| `vendor/` | (git-ignored) download cache, unpacked downloads, extracted binaries, decompilations |

## Quick start

```bash
python3 tools/ur44.py version          # ['BOOT', 'V2.12', 'MAIN', 'V2.12']
python3 tools/ur44.py mix 2            # show MIX 2 routing in dB / L-C-R
python3 tools/ur44.py set ChSendMixBus2Level 0 103   # input 1 -> MIX 2 at 0.00 dB
python3 tools/ur44.py phones2 2        # PHONES 2 listens to MIX 2
python3 tools/ur44.py monitor          # watch unsolicited changes (e.g. +48V switches)
```

The control port is found automatically: `hw:N,0,1` in Class Compliant mode (USB id 0499:1704), `hw:N,1,0` with
the [CC MODE] switch off (0499:150e). Override it with `--port`.

## The GUI

Needs Qt 6, CMake 3.16+, the ALSA development files and Python 3 (for the build-time generators).

```bash
cmake -B build
cmake --build build
ctest --test-dir build
build/gui/src/application/ur44-control
```

It finds the UR44 by its ALSA card name and owns the MIDI port while it runs, so close it before using
`tools/ur44.py` (and the other way around).

## Rebuilding the generated files

```bash
re/download_vendor.py                                    # TOOLS V2.2.2 + firmware updater (pinned SHA-256)
re/download_vendor.py --only V2111 --accept-yamaha-license   # driver V2.1.11, behind Yamaha's license page
re/download_vendor.py --group extra                      # optional: driver V2.1.7, macOS updater, macOS TOOLS V2.3.0
re/extract_vendor.sh
python3 -I re/gen_params.py vendor/extracted/tools_v222/dspmixfx_x86/memapi_ur44.dll
python3 -I re/dump_memapi_labels.py vendor/extracted/tools_v222/dspmixfx_x86/memapi_ur44.dll > tools/ur44_labels.json
re/ghidra/decompile_all.sh                               # optional, slow
```

## License

The code is under the [Microsoft Public License (Ms-PL)](LICENSE).

Not covered by it: the 34 Sweet Spot Data presets in `tools/ur44_sweet_spot.py` (and built into the GUI from it)
come with Yamaha/Steinberg's TOOLS for UR44 and carry their notice, "Copyright(C)2008 Yamaha Corporation All Right
Reserved". The UR44's control protocol was reverse engineered from TOOLS for UR44 for interoperability; this project
isn't affiliated with or endorsed by Yamaha or Steinberg.
