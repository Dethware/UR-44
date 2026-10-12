# Steinberg UR44 control protocol – working notes

Status: early RE. "Confirmed" = observed on the real device (fw BOOT/MAIN V2.12).
"Static" = read from decompiled Windows binaries, not yet confirmed live.

## Transport

* USB config 1 (class compliant, what Linux uses): UAC2 audio + one USB-MIDI
  interface (IF3, bulk EP 0x01/0x82) with two embedded ports.
  **Control traffic goes over MIDI port 2** (`hw:X,0,1`). Port 1 does not answer. *Confirmed.*
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

| Message | Bytes | Status |
|---|---|---|
| Get param | `F0 43 30 3E 14 01 01 <a:u16> <b:u16> F7` | confirmed |
| Reply / set | `F0 43 10 3E 14 01 01 <a:u16> <b:u16> <value:u32> F7` | confirmed both ways. A set gets no ack/echo; read back to verify |
| Set variant | 7th byte is `03` instead of `01` when a flag in the request struct is set | static |
| Version | `F0 43 30 3E 14 00 70 02 F7` → `... 00 70 02 00 "BOOT" .. "V2.12 " .. "MAIN" .. "V2.12 " F7` | confirmed |
| Others | `03 02 01 <u16>`, `01 01 <u32>` (dump req), `02 02 32 <00/7F>`, `03 09 01 <b>`, `03 08 ..`, text payload | static; builders FUN_10009170..FUN_10009c10, dispatcher FUN_10008d90 |

`a` looks like a parameter ID and `b` like a channel/instance index (see below).

## Parameter model

memapi holds a table of 177 descriptors (`PTR_DAT_10022118`, ids 0x00–0xB0) with
name, group mask, min, max and default. Dump: `docs/memapi_param_table.txt`
(produced by `tools/dump_memapi_params.py`).

**Confirmed: `a` = table id, `b` = instance index.** See the 2-D read sweep in
`docs/scan2d.txt` (rows = id, columns = index 0..11). Instance counts observed:

| Group | Ids | Valid indices |
|---|---|---|
| Ch* (input channels) | 0x00–0x10, 0xAF–0xB0 | 0–5 (6 inputs) |
| Daw*/Master*/Patch*/Sys*/FxRev*/GtrFx* | various | 0 only |
| HaP48VOnf / HaInputGain | 0x28–0x29 | 0–2 |
| Fx* (channel-strip DSP instances) | 0x2A–0x3F, 0x51–0x52 | 0–3 |
| Morph* (morphing table points) | 0x53–0x63 | 0–8 (index 9+ wraps) |
| PatchLoopbackMode | 0x64 | 0 only |

Out-of-range indices return a reply whose value decodes to 0x040480C0 (or other
junk). Treat it as "invalid", not as data. A request whose reply arrives after the
timeout desyncs a naive request/response loop. Match replies on the echoed (a, b)
before trusting them (done in tools/ur44.py `wait_for`).

## Key binaries (from TOOLS_for_UR44 V2.2.2)

* `dspmixfx_ur44.exe` – Qt4 GUI.
* `memapi_ur44.dll` – COM "MemAPI UR44", parameter model and SysEx encoder/decoder.
* `ysusb_bulklib32/64.dll` – COM transport into the YSUSB driver.
* `ur44_extension.dll`, `ysusb_aadm_extension.dll` – Cubase integration plugins.
* `UR44_firmware_V212.exe` – statically linked Qt updater; firmware is embedded as Qt
  resource `:/upd_ur44_boot_main_v212.bin`. It uses the same IUsbBulkPort.

## Mix buses (manual + device state)

* MIX 1 → PHONES 1, MAIN OUTPUT, LINE OUTPUT 1/2. MIX 2 → LINE OUTPUT 3/4.
  PHONES 2 = MIX 1 or MIX 2, selected by `PatchAuxPhoneOutput` (0x22): 0 = MIX 1, 1 = MIX 2. **Confirmed by ear.**
* Per mix: `ChSendMixBus{1,2}{Onf,Level,Pan,SoloOnf}` x 6 inputs (index 0 = input 1;
  `ChSendMixBus{1,2}Onf` **confirmed by ear** on both mixes), `DawSendMixBus{1,2}*`,
  `FxRevMixBus{1,2}Onf`/`FxRevBus{1,2}Level`, `MasterMixBus{1,2}*`.
* USB playback has 4 channels. PipeWire's HiFi profile splits them into the "Line1" (1/2) and
  "Line2" (3/4) sinks. Hypothesis, not yet confirmed: Line1 feeds the MIX 1 DAW strip and Line2
  feeds the MIX 2 DAW strip.

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
* The "default" column in `docs/memapi_param_table.txt` is the Windows software's reset/initial
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
where `labels[0]` is the label of `min`, produced by `tools/dump_memapi_labels.py`.

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
