# TODO

Outstanding work and ideas. Protocol details are in [docs/PROTOCOL.md](docs/PROTOCOL.md), the reverse engineering
notes in [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md).

## To verify on the device

- [ ] **Guitar Amp Classics by ear** with a real guitar or bass (a microphone isn't a fair test). In particular the
      Clean model's PRESENCE knob, which is mapped to `GtrFxCleanHiTreble` as a guess.
- [ ] **INS.FX / MON = insert point Pre / Post.** Inferred from the manual and MemAPI, never checked by ear
      (e.g. record through the DAW with the strip on INS vs MON and compare).
- [ ] **`HaInputGain[2]` = LINE 5/6 reference level**, with 1 = -10 dBV (from memapi's default and the manual's
      stated default). `[0..1]` presumably unused.
- [ ] **Exported `.UR44` files in dspMixFx** (macOS). Ours leave out the per-scene UI memory elements
      (`MuteStatus`, `FxInsPosStatus`, `ChFxTypeStatus`); dspMixFx's writer treats them as optional, its reader is
      untested with files that lack them.

## Reverse engineering

- [ ] **dspMixFx UI memory blobs** in `.UR44` scenes (`MuteStatus` ChSz×OutChSz, `FxInsPosStatus`, `ChFxTypeStatus`)
      and the `IO` / `ChStripPatch` / `ExtFX` scene attributes. Needed for a faithful export and to restore the
      INS/MON and effect type a channel shows while nothing is inserted.
- [ ] `MemAPI::GetSceneData`'s optional filter that leaves out items 0x1e..0x20 (master levels / MIX 1 balance):
      what sets it in dspMixFx.
- [ ] `.vstpreset` effect presets: the `Comp` chunk is the plug-in model's own state (`"0000"`, a u32 list, the
      model serialization). Decode it to import/export Channel Strip, REV-X and Guitar Amp presets. Low priority:
      TOOLS ships no factory .vstpreset files.
- [ ] Set-parameter variant with `03` as the 7th byte (PRM_INFO.flag != 0), and the monitor-matrix mode command.
- [ ] `DawSendFxBus*` (0x19–0x1b): no audible effect on the FX Bus (which feeds REV-X at a fixed level). What does
      dspMixFx or the Cubase extension use them for?
- [ ] Store command (`03 02 01`): only a handshake as far as we can tell. Does it matter for the Cubase
      extension or anything the device does internally?
- [ ] Firmware image: the 4 segment descriptors at 0x64–0xA3.
- [ ] Firmware update protocol (raw USB on the vendor configuration, per the macOS updater), see below.
- [ ] YSUSB V2.1.11 (Inno Setup 6.7) can't be extracted with innoextract 1.9; needs a newer innoextract.

## Linux integration

- [ ] **UCM profile upstream**: [alsa-project/alsa-ucm-conf#882](https://github.com/alsa-project/alsa-ucm-conf/pull/882)
      (non-CC mode with the FX Bus; checked in both modes). Follow up on review; `linux/ucm2/` holds the same files
      until it's released. The earlier profile work, #584, is merged.
- [ ] **UR22C / UR24C Direct use case fix upstream**:
      [alsa-project/alsa-ucm-conf#883](https://github.com/alsa-project/alsa-ucm-conf/pull/883). With firmware 01xx/02xx
      their Direct use case kept 4 capture channels (UCM evaluates `Macro` before `If`). Checked with alsaucm using the
      UR44 as a stand-in (bcdDevice 0212); not tested on the real devices. Follow up on review.

- [ ] **Linux packaging**: the build system is ready (install rules with GNUInstallDirs, desktop entry, icon,
      AppStream metadata, `BUILD_TESTING`, `LICENSE`/`NOTICE` in the doc directory). Left:
      - a homepage URL in the metainfo (`appstreamcli validate` warns `url-homepage-missing`; Flathub requires it)
      - screenshots in the metainfo, once there's a public place to host them
      - distribution recipes (e.g. an Arch PKGBUILD) once it's published

## Application

- [ ] Effect graphs: EQ curve, compressor transfer curve, REV-X decay graph, like the dspMixFx editors.
- [ ] Factory scenes: offer the six scenes embedded in the TOOLS Cubase templates (`re/extract_template_scenes.py`)
      as starting points, built in like the Sweet Spot presets.
- [ ] Scenes panel: reorder scenes (drag and drop); show when the device state no longer matches the last
      recalled/stored scene.
- [ ] Editing Sweet Spot Data (the 9 morph points), and saving it as `.ssd`.
- [ ] Knob pointer capture on Wayland: needs pointer-constraints support that Qt doesn't expose; capture is off there
      for now.
- [ ] Tooltips for the effect editors' combo boxes (KNEE, amp type switches).
- [ ] `tools/ur44.py`: scene save/recall commands (the GUI has them, the CLI doesn't).

## Bigger goals

- [ ] **Cross-platform firmware updater** (libusb): decode the update protocol from `vendor/analysis/ghidra/fwupd.c`
      or the macOS updater. The firmware image is extracted by `re/extract_firmware.py`.
- [ ] **macOS and Windows ports** of the GUI (the transport is ALSA-only: `AlsaTransport`).
- [ ] **Successor models** (UR44C and others) if one becomes available: their own reverse engineering, and their own
      settings group next to `ur44` (detection deliberately matches only "Steinberg UR44").
- [ ] Several devices at once: deferred. There's no serial number; only the USB port path tells units apart.

## Code

- [ ] `gui/src/shared/Json.cpp`: `requireIsType<QDir>` doesn't handle invalid characters (FIXME from the skeleton).
- [ ] `gui/src/shared/FileSystem.cpp`: Windows shortcut creation is stubbed out (TODO from the skeleton).
