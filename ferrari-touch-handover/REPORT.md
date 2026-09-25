# Xiaomi Mi 4i (ferrari) touchscreen on mainline: root cause and fix

## Summary
The Mi 4i here has a **replacement display panel** whose touch controller identifies itself as an
Atmel/Microchip maXTouch mXT336T (family 0xA4, variant 0x15, firmware 2.1.AA, 41 objects) at I2C
address 0x4a. Mainline's `atmel_mxt_ts` never got a single touch from it. Two things were wrong:

1. **Probe:** the controller returns garbage for the 3 info-block checksum bytes when they are read
   as the tail of the object-table transfer (`00 00 fc` instead of `0d dd 8d`). Upstream therefore
   failed with "Info Block CRC error". Reading the checksum as a separate transfer returns the correct
   value (0x8DDD0D, matching the calculated CRC). The vendor driver reads it separately.
2. **No touch reports:** the controller does **not report any touches (T100) until the T97 touch key
   array is enabled** (T97 instance 0 CTRL = ENABLE | RPTEN = 0x03). The controller comes up with T97
   disabled after every power cycle or reset (why is unknown: its registers can't be read back reliably), and
   the vendor driver enables it on every resume. Upstream never writes T97, so the chip
   stayed silent. This was found by bisecting the vendor driver's resume sequence on the device:
   only the T97 write matters. T7 deep-sleep cycling, CALIBRATE and T19 made no difference.

Neither needs a config upload, BACKUPNV, firmware or the "write quirk" that earlier bring-up work
added. The controller and its NVM were never damaged.

**Upstreaming:** reworked v1 patches for LKML, the step-by-step plan for pem120 and the review behind it are in
[upstream/](upstream/UPSTREAMING.md).

## Fix (patch series)
| # | patch | why |
|---|---|---|
| 1 | Input: atmel_mxt_ts - drop ferrari bring-up changes | (7.0 tree only) restore the upstream driver. Bring-up had commented out the CRC check, forced RETRIGEN, added a CRC "repair" write and added a `zero_config` (zero + BACKUPNV) sysfs |
| 2 | Input: atmel_mxt_ts - read the info block checksum separately | fixes probe (item 1); also skips a zero-length read if the info block reports no objects |
| 3 | dt-bindings: input: atmel,maxtouch: add atmel,enable-t97 | documents the new boolean property (`dt_binding_check` passes on 7.3) |
| 4 | Input: atmel_mxt_ts - optionally enable the T97 key array on start | with `atmel,enable-t97`, write T97 instance 0 CTRL = ENABLE\|RPTEN from `mxt_start()`: in deep-sleep mode before the T7 restore and CALIBRATE (the vendor order), and in T9 mode after the soft reset. Boards without the property are unchanged |
| 5 | arm64: dts: qcom: msm8939-xiaomi-ferrari: use upstream maXTouch binding | (7.0 tree only) `atmel,maxtouch` node with GPIO9/GPIO78 fixed regulators from pm8916_l6 (voltages from bring-up, not measured), CHG GPIO13 level-low, RESET GPIO12 active-low, `atmel,enable-t97`; **no `atmel,write-quirk`, no key codes** (the keys produced no T97 messages in our tests) |
| 6 | i2c: qup: revert forced DMA and custom SCL dividers | (7.0 tree only) back to stock i2c-qup; the bring-up change 5d8f10ccdc03 is not needed (E12, kernel #9) |

Design notes from the adversarial review, which ran four reviewer dimensions and adversarially verified each finding:
- The first version enabled one T97 instance per `linux,keycodes` entry. That contradicted upstream's T97 decoding
  (every T97 report is treated as a single key bitmap). It would also have changed behaviour on in-tree boards that
  already set `linux,keycodes` (l9100, gt510, mocha, matisse), and it misstated the vendor behaviour (the vendor always
  writes instances 0–2). Replaced with the explicit opt-in property and instance 0 only, which the bisect proved sufficient.
- Not handled: if the controller resets itself while the device is open, T97 stays disabled until the next start
  (documented in the commit message).

**i2c-qup: the bring-up changes are not needed (verified 2026-09-25, E12).**
- Commit 5d8f10ccdc03 changed the QUP DMA and divider logic based on a wrong theory.
- The gate tests above ran on kernel #8. That kernel was built from `msm8939/mi4i` @ 03fc2dcd **plus DS's uncommitted
  i2c-qup.c changes** (custom SCL dividers, upstream DMA heuristic), now saved on branch `ds-wip-2026-09-24`.
- A first attempt at the revert (my build with Debian clang 19, RAM-booted) never brought up USB, and pem120 had to
  force a restart with the power jumpers. The cause is unknown.
- pem120's own build (#9, pmbootstrap `--envkernel`) boots fine. It is branch `claude/ferrari-touch-stock-qup`: the
  series plus **i2c: qup: revert forced DMA and custom SCL dividers**, so i2c-qup.c is stock (identical to 464923b).
  - probe: `Family: 164 Variant: 21 Firmware V2.1.AA Objects: 41`, with no CRC error;
  - no I2C/QUP errors;
  - the touch module's srcversion is identical to the tested one;
  - no IRQs while idle;
  - 6/6 touch-downs and releases across the panel (X 142–1066, Y 34–1820);
  - pem120: "Works - screen reacts correctly" (`~/claude-touch/E12-stockqup/`).
- So the bring-up i2c-qup changes are gone from `msm8939/mi4i`. It was later rebased onto 464923b, which already has stock
  i2c-qup (see below).
- Not re-run on #9: the 10-minute and blank/unblank gates.

## Evidence (full raw data under `~/claude-touch/` on ishulappy)
- **Controller behaviour** (E02/E03/E07): reads use a flat 256-byte map indexed by the low address byte, with
  special handling for exact start addresses 0x0007 (object table), 0x0182 (T44) and 0x0183 (T5). This is
  identical on the downstream i2c-msm-v2 and the upstream QUP masters. The vendor driver sees the same map
  (its `rev_id=2` and "Config info 23 01…" are bytes of it) and works.
- **Stock driver without config upload works** (E08 "rev3": stock TWRP with the config/firmware names in its
  DTB pointed at absent files). pem120 confirmed touch working, with 4316 evdev events across the whole
  panel, and it still worked after a blank/unblank cycle. Its debug log shows the suspend/resume register
  writes: T7 0/0/0, T97 CTRL 0x614/0x61e/0x628, T19 0x3a7, then CALIBRATE.
- **Mainline bisect** (E09, harness module): each case ran after a fresh module reload (power cycle + reset),
  with a human touching continuously, and IRQs counted over 9 s:
  none 0 · CAL 0 · T7→0 0 · T7 restore 0 · T7 cycle 0 · T7 cycle+CAL 0 · upstream block T7 cycle+CAL 0 ·
  T97×3+T19 440 · all vendor steps 632 · T97×3 423/410 · T19 only 0 · **T97 instance 0 only 413**.
- The capacitive keys produced no T97 messages in any of our tests (rev3 TWRP, mainline), although the MIUI log shows
  T97 key events (HANDOVER.md, stock logs). The cause is unknown, so the final series describes no key codes.

## Status on mainline 7.0 (`7.0.0-msm8916` #8, final patched `atmel_mxt_ts.ko` in rootfs **and initramfs**, final DTB)
All gates were re-run on the **final** version (driver = the committed series, DTB built from it; no manual steps after boot):

| gate | result (final version) |
|---|---|
| real touches produce evdev events, confirmed by pem120 | ✅ boots 1–3: 11/11/10 touch-downs, full panel, 2 MT slots; pem120: "Works – screen reacts correctly to taps and drags" each time |
| 10 min normal use, no I2C errors / IRQ storm / stuck contacts | ✅ 634 s logged: 2406 events, 31 downs / 31 ups, all slots released, X 30–1073 / Y 10–1902; no touch/I2C/QUP messages (only unrelated wcn36xx Wi-Fi BMPS messages); pem120: "Worked well the whole time". Idle IRQ rate: 0 IRQs and 0 evdev events in 45 s (2026-09-25, final version, 1 h uptime, no I2C/QUP errors in dmesg); the earlier 22 in 30 s right after this run was residual touching |
| survives 3 boots | ✅ |
| survives 3 blank/unblank cycles (input inhibit → `mxt_stop()`/`mxt_start()`) | ✅ 14/9/13 touch-downs in the windows after the three cycles |
| minimal reviewed series committed | ✅ both trees (see commit logs); 4-dimension adversarial review + checkpatch; the 7.3 driver compiles cleanly with W=1 at every commit; `dt_binding_check` passes |

A first run of the 10-minute test was cut short after about 4 min. pem120 rebooted the phone because the **display** did not wake after
the screen blanked: a power-key press was followed by `mdp5_irq_error_handler ... errors: 04000000`. That is a separate display
issue and not touch related. For the repeat run the Phosh idle-delay was set to 0 temporarily (original value 300 s). It was restored to
300 on 2026-09-25.

Remaining cosmetic message: at probe, before `mxt_start()` enables T97, the first IRQ logs
"T44 count 153 exceeded max report id" / "Unexpected invalid message" once or twice. It is harmless.

## Deployment notes (phone)
- Backups: `/boot/msm8939-xiaomi-ferrari.dtb.pre-claude`, `/boot/initramfs.pre-claude`,
  `/lib/modules/7.0.0-msm8916/kernel/drivers/input/touchscreen/atmel_mxt_ts.ko.pre-claude`.
- The initramfs was updated by replacing only `atmel_mxt_ts.ko` inside it. A future `mkinitfs` run will pick up
  the rootfs module, which is the same file.
- Rescue: in lk2nd fastboot, `fastboot boot ~/claude-touch/rescue/rescue-boot.img` (original kernel + DTB +
  initramfs, RAM only).
- `Signed-off-by:` lines are intentionally absent. The humans submitting these patches must add their own.

## Status of DS's checkout (2026-09-25)
- DS's uncommitted work from `msm8939/mi4i` (i2c-qup.c, atmel_mxt_ts.c, atmel_mxt_ts_336t.c, ferrari DTS) is saved on branch
  `ds-wip-2026-09-24` and as `~/claude-touch/ds-wip-2026-09-24/ds-wip.diff`. It was no longer in the working tree after the
  checkout was moved by hand on 2026-09-25.
- For pem120's stock-i2c-qup build the checkout was put on **`claude/ferrari-touch-stock-qup`**: `claude/ferrari-touch`
  plus the i2c-qup revert, so `i2c-qup.c` is the stock pre-hack version (identical to 464923b). It is the only file that differs from the
  tree the gates ran on.
- pem120 built it as kernel #9 and touch works (E12, above). `msm8939/mi4i` was first fast-forwarded to it
  (03fc2dcd → 9988b32e8fd8).
- Then, at pem120's request, the series was **rebased onto 464923b94bbb**. That commit comes before DS's 336t vendor
  driver port and before the i2c-qup change 5d8f10ccdc03, and its i2c-qup is stock. **`msm8939/mi4i` =
  `claude/ferrari-touch-rebased` = 9d28c6b714ff** (checked out):
  - c1ffcb40270c leds: lm3533, backlight: lm3533_bl: add missing includes (464923b itself does not build)
  - 231dfd6898e3 Input: atmel_mxt_ts - restore the info block CRC check (undoes the bring-up warning-only change)
  - d49f1b3da33f Input: atmel_mxt_ts - read the info block checksum separately
  - 9ebd65f703bd dt-bindings: input: atmel,maxtouch: add atmel,enable-t97
  - b64fb0cfc76d Input: atmel_mxt_ts - optionally enable the T97 key array on start
  - 9d28c6b714ff arm64: dts: qcom: msm8939-xiaomi-ferrari: fix the touchscreen
- Compared with the kernel #9 tree, the touch driver, binding, DTS and i2c-qup.c are byte-identical. The unused 336t
  driver is gone. With pem120's config, those files and the lm3533 drivers compile, and the DTB is byte-identical to #9's.
  **This branch has not been booted yet.** The rebased commits are unsigned (see NEXT-STEPS.md).
- `claude/ferrari-touch` (kernel #8 tree) and `claude/ferrari-touch-stock-qup` (kernel #9 tree) are kept for reference.
