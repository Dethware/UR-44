# Steinberg UR44 control protocol – working notes

Status: early RE. "Confirmed" = observed on the real device (fw BOOT/MAIN V2.12).
"Static" = read from decompiled Windows binaries, not yet confirmed live.

## Transport

* USB config 1 (class compliant, what Linux uses): UAC2 audio + one USB-MIDI
  interface (IF3, bulk EP 0x01/0x82) with two embedded ports.
  **Control traffic goes over MIDI port 2** (`hw:X,0,1`). Port 1 does not answer. *Confirmed.*
* No unique unit ID exists: the USB descriptor has no serial (iSerial 0; udev's ID_SERIAL is just
  vendor_product), and no known command returns one. Only the USB port path (udev ID_PATH, the ALSA card
  longname "... at usb-0000:0c:00.0-3") tells two units apart. The app supports one UR44 for now.
* snd-usb-audio starts reading the MIDI input only on the first `read()`/`poll()` of the port. A reply that arrives
  before that is lost, so read once right after opening, before sending the first request.
* ALSA rawmidi opens the port exclusively: a second `tools/ur44.py` while one is running (e.g. `meters`)
  fails with EBUSY. A UI should be the single owner of the port, multiplexing meters, gets/sets and
  notifications. Or it could use the ALSA sequencer, where several clients can subscribe to the port.
* USB config 2 (vendor mode, Windows YSUSB driver): all interfaces vendor class,
  plus an extra vendor bulk pair on IF4 (EP 0x03 OUT / 0x84 IN). The Windows tools
  reach the device through `ysusb_bulklib` (COM, `IUsbBulk*`, DeviceIoControl into
  the driver). We don't need this path on Linux.

## SysEx framing (Yamaha style) – static: memapi_ur44.dll `.rdata` 0x10019354..

```
F0 43 <t> 3E 14 ...  F7       43 = Yamaha, 3E = group, 14 = UR44 model
  t = 0x10  parameter change (host->dev set, and dev->host reply/notify)
  t = 0x20  dump request
  t = 0x30  parameter request
```

Numbers are packed big-endian in 7-bit groups:
u16 → 3 bytes `(v>>14)&3, (v>>7)&7F, v&7F`; u32 → 5 bytes (top group 4 bits).

The byte after the model ID selects a message class: **00** System, **01** Memory
(parameters), **02** Status (meters), **03** Function. Bulk dumps use the separate Yamaha bulk header.
Source: macOS MemAPI 2.3.0 with symbols (`ParameterChange::convert*`, `BulkDump::*`), cross-checked
against the Windows builders. The host-side command numbers are `IModelService::PJ_COMMAND` values
(`ParameterChange::convertToSysEx`).

### Host → device

| PJ_COMMAND | Message | Bytes | Status |
|---|---|---|---|
| 10 | Get param | `F0 43 30 3E 14 01 01 <id:u16> <idx:u16> F7` | confirmed |
| 0 | Set param | `F0 43 10 3E 14 01 {01\|03} <id:u16> <idx:u16> <value:u32> F7`. The 7th byte is `03` when PRM_INFO.flag != 0 (meaning unknown) | `01` confirmed |
| 9 | Get version | `F0 43 30 3E 14 00 70 02 F7` | confirmed |
| 4 / 5 | Meter start / stop | `F0 43 30 3E 14 02 02 32 {7F\|00} F7` (`METER_START`/`METER_STOP`) | **confirmed**: frames stream at 20/s until stopped |
| 1 / 6 | Store (scene / morph table) | `F0 43 30 3E 14 03 02 01 <n:u16> F7`, n = data type 1 or 2. Device answers with Function `03 02 01` | **confirmed**: a handshake, no visible effect, doesn't survive a power cut (see "Persistence") |
| 3 / 8 / 0xc | Bulk dump request | `F0 43 20 3E 14 01 01 <n:u32> F7`, n = 1 (current scene), 2 (morph table), 3 (all data) | **confirmed** for 1 and 3 (one reply each, ~3.5 KB); 2 gets no reply |
| 2 / 7 | Bulk dump: send scene / morph table | `F0 43 00 3E <lenH> <lenL> 14 01 01 <n:u32> <data7> <cs> F7`, n = 1 scene, 2 morph table | morph table and scene **confirmed** (see "Sweet Spot Data" and "Scenes and presets" below). **Overwrites device state** |
| 0xb | Send version (host → device) | `F0 43 10 3E 14 00 01 00 <text, '\n'→00> F7` | static |
| 0xd | Monitor-matrix control mode | `F0 43 10 3E 14 03 09 01 <mode:u8> F7` | static |
| 0xe / 0xf | FX assign / unassign | assign `F0 43 10 3E 14 03 08 01 <fx type> <ch:u16> <point> F7`, unassign `… 03 08 02 <fx type> <ch:u16> 00 F7`. fx type 0 = channel strip, 1 = guitar FX; point 0 = Pre, 1 = Post (MemAPI::SetFxPatch / ResetFxPatch). The device picks the strip and updates `ChFxIns*Patch` / `ChGtrFxIns*Patch` itself (no notification is sent; a get right after the assign may time out once) | **confirmed** (input 1, both types, Post; the full-state diff after unassign was identical) |

### Device → host

| Class | Message | Bytes | Status |
|---|---|---|---|
| 01 Memory | Param value (reply or unsolicited change) | `F0 43 10 3E 14 01 01 <id:u16> <idx:u16> <value:u32> F7` | confirmed |
| 00 System | Version | `F0 43 10 3E 14 00 70 02 00 <8-byte name><8-byte version> × n F7` ("BOOT"/"V2.12 ", "MAIN"/"V2.12 ") | confirmed |
| 02 Status | Meters | `F0 43 10 3E 14 02 02 <v:21-bit as 3×7bit> × 44 F7` (140 bytes, 20 frames/s) | **confirmed**; see "Meter frame" below |
| 03 Function | Store completed | `F0 43 10 3E 14 03 02 01 … F7` → status event 0 | static |
| bulk | Bulk dump | `F0 43 00 3E <lenH> <lenL> 14 01 01 <n:u32> <data7> <cs> F7`; n = 1 → SetSceneData, 3 → SetAllData | **confirmed**: length, checksum and unpacking all check out |

### Bulk dump encoding (BulkDump::convertChunkData / convertFormSysEx)

* `len` = 2 + 5 + len(data7), i.e. the byte count from the `01 01` through the end of the data, as 2×7 bits.
* `cs` = `-(sum of bytes from the 01 01 through the end of data7) & 0x7F` (Yamaha checksum).
* data7 = 8→7-bit packing in groups of 7: the 7 bytes' low 7 bits, then one byte holding their MSBs
  (byte k's bit 7 at bit 6−k). The decoder reads `len/8` groups.
* **The payload is self-describing** (decoder: `tools/ur44_dump.py`; samples: `docs/data/dump_*.bin`).
  It is two chunks, each a 32-byte header (16-byte name, `00 01`, zeros, `27 74`, u16 BE body length) plus a body:
  * `F_<name>`: the format. Groups `80 01 <instances:u16 BE>`, each followed by items
    `<param id:u16 BE> <flags> <size>`. Flags `01` = unsigned, `00` = signed, `40` = byte array
    followed by `<count:u16 BE>`.
  * `D_<name>`: the values in format order, item-major (every instance of item 1, then item 2, …), big-endian.
* `CurrentScene` (n=1): 9 groups, 166 items, 2240 data bytes. `All` (n=3): 11 groups, 174 items. It adds
  `PatchAuxPhoneOutput`, the meter sources, `SysFsCode`, `SysSoloOnf`, `HaP48VOnf`, `HaInputGain` and loopback.
  So "scene" is the mixer/FX state, and "All" adds the system and patch settings.
* **Verified**: all 473 values in the `All` dump equal the per-parameter reads. Groups give the instance
  counts directly: Ch 6, Morph **36** (4 channel strips × 9 points), Fx 4, MorphSystem 4, Ha 3, all others 1.
  `MorphSystemId`/`MorphSystemName` are byte arrays (4 / 256 bytes: "0001", "01 Basic"); a per-parameter
  read returns only their first byte. `DawSendFxBus*` (0x19–0x1b) is in neither dump, and has **no audible effect** (see "Class Compliant mode off"):
  the FX Bus feeds REV-X at a fixed level whatever they're set to. Per-parameter reads answer (Off, -INF, C: the
  defaults). Possibly for the Cubase integration or another model.
* `tools/ur44.py dump` now uses one "All" request (0.5 s instead of 4 s; `--slow` reads parameter by parameter).

`a` looks like a parameter ID and `b` like a channel/instance index (see below).

## Parameter model

memapi (and the macOS MemAPI 2.3.0, whose descriptors and labels are identical apart from three
renames, see REVERSE_ENGINEERING.md) holds a table of 177 descriptors (`PTR_DAT_10022118`, ids 0x00–0xB0) with
name, group mask, min, max and default. Dump: `docs/data/memapi_param_table.txt`
(produced by `re/gen_params.py`).

**Confirmed: `a` = table id, `b` = instance index.** See the 2-D read sweep in
`docs/data/scan2d.txt` (rows = id, columns = index 0..11). Instance counts observed:

| Group | Ids | Valid indices |
|---|---|---|
| Ch* (input channels) | 0x00–0x10, 0xAF–0xB0 | 0–5 (6 inputs) |
| Daw*/Master*/Patch*/Sys*/FxRev*/GtrFx* | various | 0 only |
| HaP48VOnf / HaInputGain | 0x28–0x29 | 0–2 |
| Fx* (channel-strip DSP instances) | 0x2A–0x3F, 0x51–0x52 | 0–3 |
| Morph* (morphing table points) | 0x53–0x63 | 0–35: 4 channel strips × 9 points. The sweep only *looked* like wrapping at 9 because the factory tables are identical |
| PatchLoopbackMode | 0x64 | 0 only |

Out-of-range indices return a reply whose value decodes to 0x040480C0 (or other
junk). Treat it as "invalid", not as data. A request whose reply arrives after the
timeout desyncs a naive request/response loop. Match replies on the echoed (a, b)
before trusting them (done in tools/ur44.py `wait_for`).

## Key binaries

See [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) for which vendor binary does what, and where in memapi each piece lives.

## Class Compliant mode off (confirmed)

The [CC MODE] switch is read at power-on. With it off:

* The device has another USB id, **0499:150e** (CC mode: 0499:1704), and one all-vendor-class configuration:
  IF1/IF2 isochronous audio out/in (EP 0x07 / 0x86, 3 alt settings for 44.1/48, 88.2/96, 176.4/192 kHz), IF3 a bulk
  pair (EP 0x01 / 0x82), IF4 the vendor bulk pair (EP 0x03 / 0x84). The Windows driver binds both ids.
* Linux's snd-usb-audio supports it anyway (a quirk): **6 playback and 6 capture channels**, S24_3LE, all rates, and two
  MIDI devices. The control port is **`hw:N,1,0`** (the second MIDI device; in CC mode it's subdevice 1 of device 0).
  The GUI and `tools/ur44.py` pick it by the card's USB id (`/proc/asound/cardN/usbid`).
* Playback channels (measured with the meters): **1/2 = DAW 1/2 → MIX 1, 3/4 = DAW 3/4 → MIX 2, 5/6 = the FX Bus**
  (the Windows ASIO names: "UR44 Mix 1", "UR44 Mix 2", "UR44 FX Bus"). Capture: inputs 1–6, as in CC mode.
* The **FX Bus goes straight into REV-X at a fixed level**: a tone on channels 5/6 shows on neither DAW meter, and
  comes back only through the REV-X returns (`FxRevMixBus{1,2}Onf` / `FxRevBus{1,2}Level`: -34 dB at +6 dB return,
  11 dB less at -5 dB, nothing with the return off). `DawSendFxBus*`, `FxRevBypass` and `PatchFxSend` made no
  difference.
* The state dump is the same (11 groups, 174 items), so the mode only changes the USB side.
* ALSA UCM (alsa-ucm-conf 1.2.16) only matched the CC-mode ids; `linux/ucm2/` adds 0499:150e with an
  "FX Bus" output (as upstream's UR44C profile names it). Submitted as alsa-project/alsa-ucm-conf#882.

## Mix buses (manual + device state)

* MIX 1 → PHONES 1, MAIN OUTPUT, LINE OUTPUT 1/2. MIX 2 → LINE OUTPUT 3/4.
  PHONES 2 = MIX 1 or MIX 2, selected by `PatchAuxPhoneOutput` (0x22): 0 = MIX 1, 1 = MIX 2. **Confirmed by ear.**
* Per mix: `ChSendMixBus{1,2}{Onf,Level,Pan,SoloOnf}` x 6 inputs (index 0 = input 1;
  `ChSendMixBus{1,2}Onf` **confirmed by ear** on both mixes), `DawSendMixBus{1,2}*`,
  `FxRevMixBus{1,2}Onf`/`FxRevBus{1,2}Level`, `MasterMixBus{1,2}*`.
* USB playback has 4 channels. PipeWire's HiFi profile splits them into the "Line1" (1/2) and
  "Line2" (3/4) sinks. **Confirmed with the meters:** Line1 (DAW 1/2) feeds the MIX 1 DAW channel and
  Line2 (DAW 3/4) feeds the MIX 2 DAW channel.

## All channels at once (confirmed)

MemAPI's `SetPrmAllCh(id, value, send)` sends one set-parameter message with the instance index **`0xffff`**
(as a u16: `03 7F 7F`), apparently meaning "all instances", and sets every instance in its own model. dspMixFx uses it for Ctrl+Shift+drag on an input fader (all six inputs' `ChSendMixBus<n>Level`
to the dragged value; `FUN_00404de0`, only for channels 0–5) and for Ctrl+Shift+click on mute/solo (`FUN_00422ff0`).
**Confirmed:** `F0 43 10 3E 14 01 01 00 00 10 03 7F 7F 00 00 00 00 03 F7` set `ChSendMixBus2Pan` of all six inputs
to R3 in one message, left MIX 1's pans alone, and got no reply (like any set). The GUI uses it for the same
shortcuts (`Device::setParameterAllInstances`).

## Unsolicited change notifications (confirmed)

The device pushes a normal `F0 43 10 3E 14 01 01 <a> <b> <value> F7` message, the same format
as a read reply, when a hardware control changes. Observed for the +48V switches:

| Switch | Message |
|---|---|
| +48V for MIC/HI-Z 1/2 | `HaP48VOnf[0]` = 0/1 |
| +48V for MIC/LINE 3/4 | `HaP48VOnf[1]` = 0/1 |

The INPUT GAIN knobs send nothing; they appear to be purely analog (confirmed by experiment). The
manual's block diagram also leaves the INPUT GAIN, OUTPUT and PHONES knobs out of the DSP signal chain.
So the only hardware controls the protocol can see are the two +48V switches.

So a UI can stay in sync by listening on port 2. Notifications can also arrive in the middle of a
request/response exchange, which is why `UR44.wait_for` stashes unmatched messages in `unsolicited`.
Not yet checked: whether host-side writes are echoed to *other* clients. They are not echoed to the sender.

## Ha* = per input pair (partly inferred)

The `Ha*` instance index is the analog input pair: 0 = inputs 1/2, 1 = 3/4, 2 = 5/6.
* `HaP48VOnf[0..1]`: read-only mirrors of the two front +48V switches (confirmed). `[2]` is an unused slot (line 5/6 has no phantom).
* `HaInputGain[2]`: probably the LINE INPUT 5/6 reference level (+4 dBu / -10 dBV, a software setting
  per the manual). Its default and current value is 1, so likely 1 = -10 dBV. Not confirmed. `[0..1]` presumably unused.

## Persistence and "defaults"

* The device keeps its **whole** state across power cycles (**confirmed**): an `All` dump before and after
  [P] standby, unplugging, replugging and switching on matched in all 940 values, and the CurrentScene dump was
  byte-identical. That includes the system/routing settings (sample rate, PHONES 2 source, meter sources,
  loopback), the effect inserts and the morph tables, all set through this protocol.
* It's saved **only** when the [P] switch goes to standby (**confirmed**): a change (IN 6 MIX 2 pan) followed a few
  seconds later by pulling the power with [P] on was lost, and the device came back with exactly the state of the
  last standby. There's no save-as-you-go or delayed save.
* The save at standby is quick (**confirmed**): a change followed by [P] to standby and pulling the power about
  half a second later was kept, and nothing else differed.
* The **store command** `F0 43 30 3E 14 03 02 01 <type:u16> F7` doesn't save either (**confirmed**). `type` is a
  data type, not a slot: MemAPI's PJ_COMMAND 1 is store(1) (scene) and 6 is store(2) (morph table), next to the
  type 1/2 dump requests; `MemAPI::SetSceneData` sends store(1) after the scene bulk dump when told to "send and
  store". The device acknowledges with the same message as a change (`F0 43 10 3E 14 03 02 01 00 00 01 F7`),
  within ~10 ms. But a change followed by store(1), and a scene bulk dump followed by store(1) with 30+ s before
  pulling the power, were both lost (`re/probe_store.py`). It has no visible effect; only standby saves.
* **What dspMixFx uses it for: a handshake** (traced in `dspmixfx_ur44.exe`). STORE in the scene store dialog
  (`SceneStoreDlg::slotOkButtonClicked`, FUN_0042e550):
  1. `SceneMemory` store (FUN_0042bdf0): reads the current scene from MemAPI (GetSceneData), puts it with the name
     and the UI status blobs into the chosen slot of the scene list *on the computer*, then sends PJ_COMMAND 1
     (store(1)).
  2. The dialog emits `sendStoreSceneCommand` → `ToolArea::slotSendStoreSceneCommand` disables the scene list and
     the STORE button and starts a 10 s timer; then `sceneStoreCompleted` (a local signal, not the device's reply)
     → `ToolArea::slotUpdateSceneList` redraws the list.
  3. The device's reply `03 02 01 ...` is recognised by `ParameterChangeFunction::convertFromSysEx` (it matches
     only `02 01` after the class byte), which reports status 0 → event 0x3ef → `ToolArea::finishStore` enables the
     list and STORE again (also on the timer's timeout).
  So the device's reply is the useful part: it says the device has processed everything sent before it. Nothing
  is stored on the device. Our GUI doesn't need it (scenes are read from the cache and kept in the settings).
* The "default" column in `docs/data/memapi_param_table.txt` is the Windows software's reset/initial
  value, compiled into memapi. It has nothing to do with the device's stored state, and that is the
  only reliable source for defaults. Example: memapi's `HaInputGain` default is 1 and the manual's
  stated default for LINE 5/6 is -10 dBV, which suggests 1 = -10 dBV (still to be confirmed).
* dspMixFx's scene list lives on the host (see "Scenes and presets"); recall is a scene bulk dump. Its
  "Initialize All" clears that list. The store command is covered above.

## Value scales (static, from memapi display tables)

memapi has a per-parameter label table, `PTR_PTR_10025778[id] -> {char **labels, char *unit}`.
The text shown for raw value v is `labels[v]` (FUN_1000ada0). The shared integer table at 0x10022438
is the exception: it is indexed by `v - min`. The full extraction is in `tools/ur44_labels.json`,
where `labels[0]` is the label of `min`, produced by `re/dump_memapi_labels.py`.

| Kind | Raw | Display |
|---|---|---|
| Fader / send / return levels (`*Level`) | 0..127 | 0 = -INF, 1 = -74.00 dB … **103 = 0.00 dB** … 127 = +6.00 dB (non-linear table) |
| Pan / balance | -16..16 | L16 … C … R16 |
| `SysFsCode` | 0..5 | 44.1k, 48k, 88.2k, 96k, 176.4k, 192k |
| `HaInputGain` (LINE 5/6, index 2) | 0..1 | +4dBu, -10dBV |
| `PatchAuxPhoneOutput` | 0..1 | MIX 1 L/R, MIX 2 L/R |
| `PatchDawMeter` / `PatchMasterMeter` / `PatchFxSend` | 0..1 | DAW 1/2 vs 3/4, MIXBUS 1 vs 2, MIXBUS 1 vs 2 (REV-X send source) |
| `ChHpfFreq` | 0..4 | 40, 60, 80, 100, 120 Hz |
| `ChFxInsPre/PostPatch` | 0..4 | no fix (sic), CHSTRIP 1..4 |
| Comp/EQ gains | 0..360 | -18.0 … +18.0 dB, in 0.1 dB steps |
| Comp/EQ freq | 4..124 | 20 Hz … 20 kHz (table, 1/12-octave-ish) |
| Guitar amp knobs | 0..100 | 0.0 … 10.0 |
| Guitar amp output | 0..127 | -INF … 0.0 dB (a different table from the mixer faders) |

Parameters without a table (`labels: null`) are plain numbers: FxRevDiff, FxRevRoomSize, FxRevDecay,
FxMorphValue, Morph system id/name, the guitar "UserParam"/HidInput/HidMaxOut/SpAir values.
`FxRevTime` is special: dspMixFx computes its seconds from room size and reverb type
(FUN_1000ae60 / FUN_1000b850, using the 70-entry double table at 0x10019120). The static label
list in the JSON is only an approximation.

## Meter frame (live mapping)

44 values per frame = 22 **(level, peak-hold) pairs**; peak-hold keeps a peak for about 2 s. 0–19 were
mapped live; 20–43 come from the macOS FX editor plugins (`libChannelStrip_UR44.dylib`:
`GR_INDEX` = {20,22,24,26}, `EQ_INDEX` = {28,30,32,34}; `libGA_Classics_*_UR44.dylib`:
`Vcm*Editor::meterChanged` reads 36/38/40/42), then confirmed live with `re/probe_fx_assign.py`:

| Values | Meter |
|---|---|
| 0–11 | Inputs 1–6 (pairs) |
| 12–15 | DAW L, R. The source is `PatchDawMeter`: 0 = DAW 1/2, 1 = DAW 3/4 (**confirmed**) |
| 16–19 | Master L, R. The source is `PatchMasterMeter`: 0 = MIXBUS 1, 1 = MIXBUS 2 (**confirmed**). It follows the master fader and mix membership (an input switched off in the mix doesn't show) |
| 20–27 | Channel strip 1–4 **compressor gain reduction** (pairs). Use `v & 0x1FFF`: 8191 = no reduction, lower = more; ≈ (8191 − v) / 102.4 dB. Bit 15 set (0x9FFF) = no reduction (idle). **Confirmed**: strip on input 1 with drive 10 / ratio ∞ read 0x1DF3–0x1F68 (≈ 1.5–5 dB GR). Unassigned strips hold stale values (3–4 read 0x0F3F before the probes, 0 after), so ignore GR for strips not in use |
| 28–35 | Channel strip 1–4 **output level** (pairs; a stereo-linked strip pair also reads the next index). **Confirmed** live: follows the strip output (input + drive/makeup) |
| 36–43 | **Guitar amp**: 36/38 = amp input L/R (equal to the input meter), 40/42 = amp output L/R (pairs). **Confirmed** live |

Scale: **dB = value / 102.4 − 80**, i.e. 0 = −80 dB (floor) and 8192 = 0 dBFS.
* Slope: Mix 1 master 103 → 80 (−5.75 dB) moved the master peak by 589 units (102.4/dB).
* Offset: a −20 dBFS 1 kHz sine (`re/tonetest.sh 10`, i.e. speaker-test at 10 %) on Line1 reads a steady
  6126 on DAW-L/R. 6126 / 102.4 − 80 = −20.2 dB, so the formula holds within 0.2 dB. That is a peak
  meter (an RMS reading would be 3 dB lower).
* Ballistics: release ≈ 44 dB/s (about 680 units per 150 ms); the peak hold lasts ≈ 1.5–2 s.

## Effects: host-side relations and quirks

From macOS MemAPI `smdSpecialRelationTbl`. The device does *not* do these itself; a client changing these
parameters has to (implemented in `gui/src/ur44/Effects.cpp`):

* **MORPH** (`smd_SetPrmRelMorph`): `FxMorphValue[strip]` (0..120) / 15 gives a morph table point `i` and a
  fraction `f` (value 0 = exactly point 0; the last point is 8). The 17 values of points `i` and `i+1` are read
  from `Morph*[strip * 9 + point]` (0x53..0x63). `(b - a) * f + a` is truncated, clamped, and written to the
  strip's `Fx*` counterparts (FxCompAttack .. FxEqPeqGain, same order).
* **REV-X type** (`smd_SetPrmRelRevType`): when `FxRevType` changes, the reverb parameters reset to the type's
  defaults (Hall / Room / Plate): Time 23/6/21, Diff 10/8/8, IniDly 2, Hpf 4/6/12, Lpf 50/47/52, RoomSize
  29/15/18, HighRatio 8/7/9, LowRatio 12/11/10, Decay 27/15/5, LowFreq 32. `FxRevBal` is left alone.
* **Guitar amp type** (`smd_SetPrmRelGtrFxType`): when `GtrFxType` changes, the 18 parameters of the *new*
  model (`GtrFx<Clean|Crunch|Lead|Drive>*`) reset to their defaults.
* There's no host relation between REV-X time and room size. The *displayed* time is computed from type, room
  size and time (`gui/src/ur44/ReverbTime.cpp`, from `prmTable::getActualReverbTime`).

**Firmware quirk (confirmed):** the device doesn't answer a parameter read of an insert parameter
(`ChFxIns{Pre,Post}Patch`, `ChGtrFxIns{Pre,Post}Patch`) while it's non-zero, i.e. while an effect is assigned.
Other parameters answer normally, and the bulk dump has the right value. So after an FX assign/unassign, read
the state with a dump (`Device::resync()`), not with parameter reads.

## Input pairing

**Not implemented** in the GUI yet.

### On the device (confirmed, 2026-10-17, CC mode off)

Inputs 3/4 paired with `ChPairOnf[2]` and `[3]` = 1 (set directly, without MemAPI's copying), then changed through
UR44 Control (which knows nothing of pairs) with a microphone on each input, then unpaired:

* **The firmware links the pair itself.** A change to one channel is **copied into the other channel's parameter**
  (a dump shows both with the latest value), but the device **sends nothing for the other channel**, so a client
  that doesn't know about pairs shows stale values there. Linked: `ChHpfOnf`, `ChSendMixBus1Level` (with
  `ChFaderLinkOnf` on; off is untested), `ChSendFxBusLevel`, the channel strip on/off. Per channel:
  `ChPhaseOnf`, `ChSendMixBus1Pan` (centre stays centre on both: the pans are what separates the channels).
  Untested: MIX 2, the solos, the send switches.
* **The channel strip on a pair** becomes a stereo strip: switching it on for the pair gave input 3 `CHSTRIP 1`
  and input 4 `CHSTRIP 2`, and set `FxStereoLinkOnf[0]` and `[1]`. The two strips' `Fx*` values stay identical
  (edits are mirrored), but the Sweet Spot data (`Morph*` tables, `MorphSystemId/Name`) isn't: loading a preset on
  the pair only changed strip 1's tables.
* **Unpairing** switches the stereo link off and keeps every channel setting, but **swaps the strips**: input 3
  got `CHSTRIP 2` and input 4 `CHSTRIP 1`, each strip's morph tables moving with it (input 3 kept its preset).
* **USB is unaffected**: no re-enumeration, same descriptors, the ALSA streams unchanged (6 + 6 channels). The
  pair isn't visible to the computer; recording still gets two mono channels.

### MemAPI (static)

From macOS MemAPI (`smd_SetPrmRelPair`, `smd_SetPrmRelFaderLink`, `CsmdModel::_GetGroupInfo`, `setChStripPatch`)
and dspMixFx. MemAPI does the same linking on its side (on its copy of the state, which it then sends); with the
firmware doing it too, it's at least how dspMixFx keeps its own display in step.

* The pairs are inputs 1/2, 3/4, 5/6 (channels `2k` and `2k+1`). dspMixFx has a "Link Channels" button per pair.
* **`ChPairOnf[ch]`**: the pair switch, set on both channels of the pair. **`ChFaderLinkOnf[ch]`** (default 1): while
  paired, whether the two channels' MIX 1/2 levels move together. **`FxStereoLinkOnf[strip]`**: two channel strips
  working as one stereo strip.
* MemAPI keeps a 19-byte block per channel, in this order: pair, fader link, phase, HPF on, HPF frequency,
  `ChFxIns{Pre,Post}Patch`, `ChGtrFxIns{Pre,Post}Patch`, MIX 1/2 solo, MIX 1/2 on, MIX 1/2 level, MIX 1/2 pan,
  FX Bus on, FX Bus level. (Inferred from which offsets the relations read and keep; consistent with all of them.)
* **Pairing on**: the left channel's block is copied to the right one, which keeps its own phase, its insert
  patches, its MIX 1/2 pans, and (with the fader link off) its MIX 1/2 levels. Then these items are sent for both
  channels (`pairItemTbl`): `ChPairOnf`, `ChHpfOnf`, `ChHpfFreq`, `ChSendMixBus{1,2}SoloOnf`,
  `ChSendMixBus{1,2}Onf`, `ChSendFxBusOnf`, `ChSendMixBus{1,2}Level`, `ChSendFxBusLevel`.
  * A channel strip on the left channel is taken off both channels and patched again as a stereo strip: two
    adjacent strips, stereo linked (MemAPI moves strip blocks around, `copyChStripBlock`, to free two adjacent
    ones; with four strips, at most two pairs can have one). A stereo-linked strip pair also shows on the next
    strip output meter (see "Meter frame").
  * Guitar amp patches are taken off both channels (the amp is mono).
  * The solo state flag (`SysSoloOnf`) is recomputed.
* **While paired**, a change to one channel is applied to both (`_GetGroupInfo`), except `ChPairOnf`,
  `ChFaderLinkOnf`, `ChPhaseOnf` and `ChSendMixBus{1,2}Pan` (`notGrpItemTbl`), and the MIX 1/2 levels while the
  fader link is off (`faderLinkItemTbl`: `ChSendMixBus{1,2}Level`; the FX Bus level stays linked).
* **Fader link on** while paired: the left channel's MIX 1/2 levels are copied to the right one and sent.
* **Pairing off**: both channels keep their settings; a stereo strip is patched again as mono strips, one per
  channel.

## REV-X routing

* One REV-X, one input, two returns (**confirmed by ear**):
  * `PatchFxSend` (0 = MIXBUS 1, 1 = MIXBUS 2) picks the mix that feeds REV-X: the `ChSendFxBusLevel` sends of
    that mix's channels. The GUI's SEND button (lit on the source mix).
  * `FxRevMixBus{1,2}Onf` switches the REV-X return into MIX 1/2, at `FxRevBus{1,2}Level`. Both can be on at
    once, so a reverb fed from one mix can be heard in both. With it off the mix has no reverb. The GUI's RTN button.
* `FxRevBypass` (0/1) has no audible effect either (**by ear**): the reverb stays on with it set. The manual's
  dspMixFx window has no REV-X on/off besides SEND, and memapi only reads it when it packs the whole mixer state
  (`CsmdExModel::InitMixer`). Likely a plug-in-side setting (like `FxRevBal`) that the device only stores: its
  state survives power cycles, so the software (e.g. the Cubase extension syncing the REV-X plug-in) can get it
  back from the device. Not exposed.
* `FxRevBal` (0..127, "%") has no audible effect: 1 and 127 sound the same (**by ear**). The manual says the
  REV-X MIX and OUTPUT controls "are only available in the Plug-in version" (the Basic FX Suite REV-X, whose
  manual pictures them). Not exposed.
* There's no REV-X output meter: all 22 meter pairs are accounted for (see "Meter frame"), and the OUTPUT meter
  is also plug-in only.

## Scenes and presets (host-side files)

None of these live on the device: dspMixFx keeps its scene list on the computer. The device only has its
current state (kept over standby).

* **Recalling a scene (confirmed):** send it as a type 1 bulk dump. The device applies all of it, effect inserts
  included (`ChFxIns*`, without the FX assign command), and answers `F0 43 10 3E 14 03 02 02 00 00 00 00 01 F7`.
  Re-sending the current scene changes nothing; a changed pan or insert takes effect and re-sending the original
  restores the state exactly (`re/probe_scene.py`). The scene's format is the state dump's minus the system items
  below, so a client can build one from the state it has.
* **Timing and queueing (confirmed):** a scene (3456 bytes on the wire) is out of the host in ~33 ms, and the
  device acknowledges it ~300 ms later. Everything sent behind it is queued and handled strictly in order: a read
  and a store sent right after it were answered right after the acknowledgement; two scenes back to back plus 20
  sets of one parameter were all applied in order (acks at 362 / 603 ms, the store reply at 622 ms, the last set
  won).
* **But the input buffer is small, and overflow is silent (confirmed):** the device takes USB data at full speed
  (no backpressure: ten scenes went out in ~33 ms each) and keeps only the scene being applied plus about one more
  (~7 KB). With 3 or 4 scenes back to back, only 2 were acknowledged, and a store command sent behind them was lost
  too. So a sender has to keep at most one bulk dump unacknowledged (the `03 02 02 <type>` reply) and send what
  comes after it once it's accepted. `Device::sendBulk()` does this, and queues small messages behind a pending
  bulk dump to keep the order (**confirmed** in use: rapid scene recalls end on the last one).
* **A scene is the `CurrentScene` payload**: `MemAPI::GetSceneData` is `GetCnvFileData("CurrentScene", 0x2774)`, the
  same F_/D_ chunk pair as a type 1 bulk dump. Stored on the host, the 32-byte chunk headers are **little-endian**
  (`74 27 00 00` at 24, u32 size at 28); the bodies are the same as the device's. Contents (9 groups):
  per input (6) pair/fader link, phase, HPF, the FX insert patches, both mixes' on/level/pan/solo, REV-X send;
  DAW sends for both mixes; the 4 morph tables with their Sweet Spot names; the 4 channel strips; REV-X (incl.
  returns); the guitar amp (all 4 models); master on/level/balance; `PatchFxSend`. **Not** in a scene: sample rate,
  PHONES 2 source, meter sources, loopback, solo state flag, +48V and the input gains (the `All` dump's extra items).
  So recalling a scene doesn't touch the routing/system settings.
  `GetSceneData` can optionally leave out the items with ids 0x1e..0x20 (the master levels / MIX 1 balance); what
  sets that option isn't known.
* **Scene file (`*.UR44`)**: XML, `<!DOCTYPE UR44>`:
  ```
  <UR44File><Header>UR44 File</Header><Version>1.0.0</Version>
    <SceneData>
      <Scene No="1..20" name="..." sz="bytes" cvtSz="base64 length" [IO=".."] [ChStripPatch=".."] [ExtFX=".."]>
        base64 scene payload
        <MuteStatus ChSz=".." OutChSz="..">base64</MuteStatus>
        <FxInsPosStatus ChSz="..">base64</FxInsPosStatus>
        <ChFxTypeStatus ChSz="..">base64</ChFxTypeStatus>
      </Scene>  (up to 20, empty slots left out)
    </SceneData>
    <CurrentScene>n</CurrentScene>
    <CurrentData sz=".." cvtSz=".." IO="0" ChStripPatch="0" ExtFX="0">base64 + the same three status elements</CurrentData>
  </UR44File>
  ```
  (from the writer in `dspmixfx_ur44.exe`, FUN_00429790 / FUN_004284b0; no sample file). The three status blobs are
  dspMixFx UI memory per channel (×output for the mutes): presumably the INS/MON position and Ch.Strip/Guitar Amp
  choice a channel shows while no effect is inserted, and the mute memory. Their layout isn't decoded.
* **Factory "scenes"** (`steinberg_ur44_*` in TOOLS): Cubase AI 7 project templates, each with the UR44 extension's
  scene embedded after a `MemAPI` tag. `re/extract_template_scenes.py` pulls them out as decodable payloads:
  | Template | Inserts | REV-X | Sweet Spot tables (strips 1-4) |
  |---|---|---|---|
  | guitar_vocal | IN 1 Guitar Amp (Crunch) MON, IN 2 Ch.Strip 1 MON | Plate 1.5 s | 08 MR Vocal, 01 Basic ×3 |
  | guitar_vocal 2 | IN 1 Guitar Amp (Crunch), IN 2/3 Ch.Strip 2/1 | Plate 1.5 s | 08 MR Vocal, 19 IO A.Guitar, 01 Basic ×2 |
  | multi_channel | IN 1/2 Ch.Strip 2/1 | Hall 2.6 s | 01 Basic ×4 |
  | stereo_recording | IN 1-4 Ch.Strip 1-4, pairs 1/2, 3/4 | Room 0.4 s | 01 Basic ×2, 02 Color ×2 |
  | vocal_inst_rec | IN 1/2 Ch.Strip 4/3 INS, IN 3/4 Ch.Strip 1/2, pair 3/4 | Plate 1.5 s | 11 SH Piano ×2, 14 OK Master - Vocal, 08 MR Vocal |
  | vocal_inst_rec 2 | IN 1-4 Ch.Strip 4,3,1,2 | Plate 1.5 s | 11 SH Piano ×2, 04 MZ A.Guitar, 08 MR Vocal |
  All have every input at 0 dB/centre in both mixes (only the REV-X sends differ), both REV-X returns on and REV-X
  fed from MIX 1. The four templates without a guitar amp also lack the guitar amp group (8 groups instead of 9).
* **Effect presets (`*.vstpreset`)**: dspMixFx's effect editors are the VST3 plug-ins themselves, and they save the
  standard Steinberg preset container (`VST3` header, class id, chunk list with `Comp`/`Cont`/`Info`). The `Comp`
  chunk is the plug-in's own state: `"0000"`, a list of u32 BE, then the plug-in model's serialization
  (`MyVst::Component::getState` in the REV-X plug-in). The Channel Strip plug-in also has a `readSSD`, so its
  presets likely carry the Sweet Spot table. Not decoded further; no factory .vstpreset files ship with TOOLS.

## Sweet Spot Data (morph tables)

* Presets are `.ssd` files (512 bytes): `SSMD`, version 1 (u32 LE at 8), id ("0002", 16 bytes at 0x10), then 9 points
  of 20 big-endian u16 at 0x20; the first 17 are the Morph* fields in parameter order. The name is the file name.
  TOOLS for UR44 ships 34 of them (6 "Yamaha Preset", 28 "Engineers' Preset", ids 0001..0034), with mangled names
  on Windows; `re/extract_sweet_spot.py` restores the names.
  The GUI ships them built in (`tools/ur44_sweet_spot.py`, generated from the vendor files; they carry
  "Copyright(C)2008 Yamaha Corporation").
* **Loading one (confirmed):** send a bulk dump of type 2 whose payload is a `MorphingTable` chunk pair, the
  dump format with just the MorphSystemId/MorphSystemName group (4 strips) and the Morph* group (36 = 4 x 9),
  holding the whole table with the target strip replaced. The device answers
  `F0 43 10 3E 14 03 02 02 00 00 00 00 02 F7` (Function class, data type 2). Then re-apply the strip's
  `FxMorphValue` (the morph relation) so its live compressor/EQ values follow the new table, as dspMixFx does.
  Re-sending the current table is a no-op, and loading "02 Color" into strip 4 then "01 Basic" back left the
  state identical (`re/probe_morph_table.py`).
* A type 2 *dump request* gets no reply; the table comes with the "All" dump.
