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
| 1 / 6 | Store scene (variant 1 / 2) | `F0 43 30 3E 14 03 02 01 <n:u16> F7` with n = 1 or 2. GUI `sendStoreSceneCommand` → device answers with Function `03 02 01` (`sceneStoreCompleted`) | static. **Writes device memory; don't send blindly** |
| 3 / 8 / 0xc | Bulk dump request | `F0 43 20 3E 14 01 01 <n:u32> F7`, n = 1 (current scene), 2 (morph table), 3 (all data) | **confirmed** for 1 and 3 (one reply each, ~3.5 KB); 2 gets no reply |
| 2 / 7 | Bulk dump: send scene / morph table | `F0 43 00 3E <lenH> <lenL> 14 01 01 <n:u32> <data7> <cs> F7`, n = 1 scene, 2 morph table | static. **Overwrites device state** |
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
  read returns only their first byte. `DawSendFxBus*` (0x19–0x1b) is in neither dump, so it is probably unused on the UR44.
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

## Mix buses (manual + device state)

* MIX 1 → PHONES 1, MAIN OUTPUT, LINE OUTPUT 1/2. MIX 2 → LINE OUTPUT 3/4.
  PHONES 2 = MIX 1 or MIX 2, selected by `PatchAuxPhoneOutput` (0x22): 0 = MIX 1, 1 = MIX 2. **Confirmed by ear.**
* Per mix: `ChSendMixBus{1,2}{Onf,Level,Pan,SoloOnf}` x 6 inputs (index 0 = input 1;
  `ChSendMixBus{1,2}Onf` **confirmed by ear** on both mixes), `DawSendMixBus{1,2}*`,
  `FxRevMixBus{1,2}Onf`/`FxRevBus{1,2}Level`, `MasterMixBus{1,2}*`.
* USB playback has 4 channels. PipeWire's HiFi profile splits them into the "Line1" (1/2) and
  "Line2" (3/4) sinks. **Confirmed with the meters:** Line1 (DAW 1/2) feeds the MIX 1 DAW channel and
  Line2 (DAW 3/4) feeds the MIX 2 DAW channel.

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

* The device keeps its mixer state across power cycles (confirmed by experiment). The manual says
  the current scene and the PHONES 2 source are saved when the [P] switch goes to standby. They are
  *not* saved if power is cut while the device is on.
* The "default" column in `docs/data/memapi_param_table.txt` is the Windows software's reset/initial
  value, compiled into memapi. It has nothing to do with the device's stored state, and that is the
  only reliable source for defaults. Example: memapi's `HaInputGain` default is 1 and the manual's
  stated default for LINE 5/6 is -10 dBV, which suggests 1 = -10 dBV (still to be confirmed).
* dspMixFx also has scene store/recall and "Initialize All" (deletes saved scenes), driven by
  dedicated SysEx commands. Candidates are the non-parameter builders FUN_10009170..FUN_10009c10 in
  memapi, which are not decoded yet. Don't send those blindly: they may overwrite stored scenes.

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
