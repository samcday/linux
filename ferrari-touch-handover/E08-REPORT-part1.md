# E08 — vendor-probe replay in guarded TWRP (Claude Code, continuing Codex thread)

Phone: guarded TWRP rev2 (RAM boot, both touch nodes disabled), adb `124ccc8f`,
bus `/dev/i2c-5` (i2c-msm-v2), rails GPIO9/78 high, reset GPIO12 high, CHG GPIO13 input.

## 0. E07 capture result (pulled after the Codex thread died)
`E07-ramboot/capture/final/capture-e07a-1790242628.jsonl` sha256 `7f01df7e…`
180 s, 31881 CHG samples, **0 CHG transitions**, 180 reads of T5 (0x0183, 610 B),
**1 distinct payload** (all identical), 0 errors. Human confirmation never arrived,
so this does not prove "touch produced nothing".

## 1. What the raw E02/E03 data actually says (re-derived)
- The controller exposes a **flat 256-byte map indexed by the low address byte**;
  the high byte is ignored, except at a few exact start addresses:
  `0x0007` → object table, `0x0182` → 0x99 (= flat[0x12]), `0x0183` → message stream.
  Any read that does not start exactly at 0x0007 sees flat bytes, not the table
  (read(0x0008,16) = flat[0x08..]). This is the same on mainline QUP and TWRP msm-v2.
- The vendor driver runs against the **same map**: its logged `rev_id = 0x2` is
  flat[0x15], and its `Config info: 23 01 00…` is flat[0xde]. The odd map is not by
  itself what breaks mainline.
- The static T5 "stream" (T6 RESET/CRC 0x55571E, T25, T46, T97×3, T100×12,
  T109 `05 00`, T6 CAL) is exactly the batch the vendor driver consumes at probe:
  MIUI and TWRP both log `msg for t109 = 0x5 0x0` → `Calibration start!` →
  `Config CRC 0x55571E`. CHG high with that batch pending is **normal** for this
  chip: every vendor log (MIUI, TWRP) also has `mxt_wait_for_chg() timeout!`.
- The 610-byte T5 read ends in `25 00 01 81 00 00 2c 82 01 00` = object-table
  entries 0–1, i.e. the read walks straight from the message buffer into the
  table storage. Consistent with a firmware that emulates the mXT336T protocol
  (probably the replacement screen's controller) rather than a real mXT memory map.

## 2. Vendor probe path (pristine source `downstream/atmel_mxt_ts_336t.c` c75d260e…)
MIUI log (`dmesg_stock.log` 4b6686dc…, same serial, same `Config info 23 01`):
`Config CRC 0x55571E: OK`, input registered, T97 key press/release later → **touch
worked with no config upload / BACKUPNV / reset**.
On that path the vendor driver writes only three volatile T6 commands:
`T6.DIAG(0x0193)=0x80` (rev), `T6.DIAG=0x81` (lockdown), `T6.REPORTALL(0x0191)=1`,
plain framing `[lo hi val]`. Everything else is reads. IRQ = level-low oneshot,
handler = read(0x0182, 11) then (count-1)×10 from 0x0183.

## 3. E08 phase 1 — exact replay of that sequence (`vendorprobe.c` 96ee3fd7…)
`e08-init.jsonl`: all transfers OK. T7 reads 20/09/19 (= vendor biel T7).
DIAG 0x80 read back 0x80 then 0x00 after ~10 ms (the firmware processes T6
commands); rev_id 02; lockdown 00×8 (T37 page switch works, as in MIUI);
REPORTALL → same static batch (20 valid msgs), CHG stayed high, T44 still 0x99.
No RESET/BACKUPNV/CALIBRATE/config/firmware writes were made.

## 4. E08 phase 2 — read-only vendor-IRQ emulation monitor
- `e08irq-4894` (PID 669): 4894–5263 s, 0 CHG transitions, no human confirmation; stopped.
- `e08irq2h-5263` (PID 705): runs to ~12464 s uptime, waiting for pem120's touches.

## 5. Findings about the mainline tree
- The `atmel,write-quirk` / `mxt_write_quirk` ("first data byte ignored, write at
  reg-1") came from a misreading: DS's own wmap test (write `[00 aa bb]` @0x38d
  changed 0x8e/0x8f) is what plain writes do when flat[0x8d] already holds 00.
  The vendor driver uses plain framing and its config upload worked on 09-19.
  With the quirk on, every mainline write also writes a stray 0 to reg-1.
- Since the last known-good touch (stock TWRP 09-19), DS made many persistent
  writes: BACKUPNV (quirk and plain), T18 00 00 + BACKUPNV (T18 00 00 matches the
  vendor config, so harmless), T38 + BACKUPNV, config via mem_access + BACKUPNV,
  RESET=0xA5 bootloader-entry attempts, update_fw. The chip still reports T38 v0x23
  and CRC 0x55571E (MIUI's config, not the biel v0x22 file), so either its
  "NVM" ignores BACKUPNV or those values are canned.
- The stock TWRP kernel has the configs built in (biel 164865, tpk B95147, wintek
  D05D49): **every stock TWRP boot uploads biel + BACKUPNV + reset** on this chip.

## 6. Prepared, not yet used: `rev3-TWRP.img` (e350af22…)
Stock TWRP with Synaptics disabled and the Atmel node **enabled**, with every
`atmel,mxt-cfg-name` and `atmel,mxt-fw-name` pointed at absent files. The vendor
driver then takes the MIUI path with no config upload, no BACKUPNV, no reset, and no
bootloader entry (`mxt_load_fw` fails at request_firmware before touching the chip).
DTB diff vs original = exactly those 6 lines. Splice + id digest per E06 (formula
re-verified against the original image). Needs a controlled reboot to fastboot.
Caveat: TWRP UI will receive touches → grab the evdev node before the human test.
