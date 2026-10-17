# TODO

Outstanding work and ideas for the reverse engineering and the code. Protocol details are in
[docs/PROTOCOL.md](docs/PROTOCOL.md), the reverse engineering notes in
[docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md). Releases, packaging and upstream contributions are tracked
outside the repository, in the workspace's `../TODO.md`.

## To verify on the device

- [ ] **Guitar Amp Classics by ear** with a real guitar or bass (a microphone isn't a fair test). In particular the
      Clean model's PRESENCE knob, which is mapped to `GtrFxCleanHiTreble` as a guess.
- [ ] **INS.FX / MON = insert point Pre / Post.** Inferred from the manual and MemAPI, never checked by ear
      (e.g. record through the DAW with the strip on INS vs MON and compare).
- [ ] **`HaInputGain[2]` = LINE 5/6 reference level**, with 1 = -10 dBV (from memapi's default and the manual's
      stated default). `[0..1]` presumably unused.
- [x] **What the device does with `ChPairOnf` by itself**: it links the pair in the firmware (copies changes to the
      partner without reporting them, stereo strip, strip swap on unpairing); USB unaffected. See docs/PROTOCOL.md.
- [x] **Pairing, the rest**: MIX 2, solos, send switches, pans, the fader link, the copy direction (left to right),
      whether anything is reported (nothing is). See docs/PROTOCOL.md.
- [x] **The guitar amp on a paired channel**: pairing removes it; inserted on a paired channel, it's accepted on
      that channel only. See docs/PROTOCOL.md.
- [ ] **The fader link, by experiment**: which faders a pair has and which levels each one controls, with
      `ChFaderLinkOnf` on and off (MemAPI: with it off, the two channels' MIX 1/2 levels are separate; the newer
      dspMixFx shows one fader per pair when linked).
- [ ] **Exported `.UR44` files in dspMixFx** (macOS). Ours leave out the per-scene UI memory elements
      (`MuteStatus`, `FxInsPosStatus`, `ChFxTypeStatus`); dspMixFx's writer treats them as optional, its reader is
      untested with files that lack them.

## Reverse engineering

- [ ] **Input pairing's strip slots**: how `setChStripPatch` picks and moves strip slots (`copyChStripBlock`) for
      a stereo strip, in the edge cases (all four strips in use, a strip on the right channel only). Only its
      main path is read.
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

- [ ] `linux/ucm2/`: keep the same as the UR44 UCM change submitted upstream until alsa-ucm-conf releases it (its
      review and the other upstream work are tracked in the workspace's `../TODO.md`).

## Application

- [ ] **Device communication redesign** (planned instead of small fixes). Known issues to cover:
      - **Knob flicker at an end of its range**: dragging a knob past -INF (or its maximum) and on makes it
        alternate with one step above. The pointer is warped back after every move event (X11), and the warp's
        own event can land a pixel off the target (logical vs. device pixels), e.g. (3400,1023) for (3401,1023);
        an upward one counts as a move up. With a warp per event, these echoes look like real 1 px moves.
        An idea (untested): warp only after a long drift, and skip events until the pointer shows up at the target.
      - **Stale confirmation replies**: each change is re-read ~150 ms later; a re-read sent just before a newer
        change returns the old value and is applied anyway, so the UI briefly shows the old value (the device
        itself ends right). Replies older than the value's latest change should be dropped.

- [x] **Input pairing**: the LINK chain per pair (right-click: fader link), the firmware's rules applied to the
      app's state (`gui/src/ur44/Pairing.cpp`), a pair's shared controls shown once, Sweet Spot data written to both
      strips of a stereo strip, no guitar amp on a pair.
- [ ] **Undo/redo** on top of the Device state: a layer between Device and the UI that records changes (and their
      previous values) as undoable steps. dspMixFx has none (its only undo is a text field's); scenes are the
      closest thing it offers.
- [x] Channel Strip graphs: compressor transfer curve and EQ response, from the editor plug-in's own model
      (`gui/src/ur44/ChannelStripCurves.cpp`)
- [x] REV-X decay graph, from the editor plug-in's model (`gui/src/ur44/ReverbShape.cpp`), with its auto zoom
- [x] REV-X graph: handles on what they set (initial delay, decay, reverb time, the ratios, low frequency, HPF, LPF),
      the wheel zooms, AUTO follows the reverb time (dspMixFx: handles on rulers, zoom buttons)
- [x] EQ graph: dragging the band points and the mid band's width (dspMixFx's graph is editable)
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
