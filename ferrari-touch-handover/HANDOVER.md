# Xiaomi Mi 4i (ferrari) touchscreen on mainline — handover

> **Update (2026-09-25): solved. Current state and next steps: [NEXT-STEPS.md](NEXT-STEPS.md). Details:
> [REPORT.md](REPORT.md).** Root cause: the controller only reports touches after T97 instance 0 is enabled, and the
> info-block CRC must be read separately.
>
> Where the fix is: the patch series on this branch (7.3), and in DS's tree `msm8939/mi4i` = `claude/ferrari-touch-rebased`
> (the series rebased onto 464923b on 2026-09-25).
>
> Verification:
> - kernel #8 passed 3 boots, 3 sleep/wake cycles and 10 minutes of use;
> - kernel #9 (stock i2c-qup) passed taps;
> - pem120 confirmed each.
>
> Since then, the OpenCode server requires a password. Statements below about dead keys and about i2c-qup are
> superseded. The rest of this file is the mid-investigation handover, kept for history.


Status as of 2026-09-24 ~12:00 UTC. Written by Claude Code, which took over the
Codex thread `01a0d25f…` for Sam. The goal is still open: touch is **not yet**
working on mainline.

## TL;DR
- **The chip is fine and needs no config upload, BACKUPNV or firmware.** pem120
  confirmed touch working in `rev3`, a RAM-booted stock TWRP running the **stock
  Xiaomi driver with its config/firmware names pointed at files that don't
  exist**. It still worked after a suspend/resume cycle.
- The controller on the replacement screen is not a real mXT336T memory map. It
  looks like firmware imitating the mXT protocol (details below). Mainline's
  failure comes from **what the driver writes and how it reads**, not from damage.
- The stock driver starts the chip reporting through its **runtime register
  sequence** (logged exactly below). Replaying only its probe commands from
  userspace (T6 diag/REPORTALL) did **not** start it.
- The capacitive keys (T97) produce no messages at all on this panel. That's a
  hardware limitation; ignore it for the touch goal.
- Next: E09. Boot mainline, load the prepared test module and bisect which part
  of the vendor sequence is needed, then write the minimal patch (below).

## Where things are
| what | where |
|---|---|
| Remote access | OpenCode server on ishulappy (Sam has the URL). **It has no auth and is on Tailscale Funnel: anyone with the URL gets a shell as `ishu`.** Shell via `POST /session/<id>/shell {"agent":"build","command":…}` |
| Sessions | DS (OpenCode agent, deepseek) `ses_f369b84d3ffe0Up2WQUyK8w4OP`; Claude's helper shell session `ses_f2d09a645ffevmQnJc0IpS3MsG` (used only for `/shell`, no LLM turns) |
| Evidence (persistent) | `/home/ishu/claude-touch/` on the laptop: `E02 E03 E06-twrp-readonly E07-ramboot E08/ (REPORT.md, rev3/, replies/, tools) E09/ (source, patch, built .ko) src/ (pristine vendor driver, biel config, stock-TWRP dmesg) boot/ (lk2nd log) ask.sh` |
| **Lost** | The laptop rebooted at 17:07 IST (11:37 UTC) and `/tmp` is tmpfs, so all of `/tmp/opencode/*` is gone: E02–E08 working dirs, rev2/rev3 TWRP images, downstream sources, DS's tools. What mattered was restored from Claude's copies into `~/claude-touch`. |
| Tools | in this directory: `rsh` / `rpost` / `rput` (shell, API and chunked upload over the OpenCode API, with retries), `ask.sh`, `vendorprobe.c`, `evgrab.c`, `cycle.sh`, `build_rev3.py`, `e09.patch` |
| DS checkout | `/home/ishu/Projects/Android/msm8939/linux`, branch `msm8939/mi4i` @ `03fc2dcd`, **with uncommitted WIP from DS** in the ferrari DTS, i2c-qup.c, atmel_mxt_ts.c and atmel_mxt_ts_336t.c. Preserve it on a backup branch before committing anything. Build: `make ARCH=arm64 O=$PWD/.output LLVM=1` (external module: `M=<dir>`). |
| Stock logs | MIUI: `/home/ishu/Projects/Android/msm8939/dmesg_stock.log` (`Config CRC 0x55571E: OK`, keys worked). Stock TWRP 09-19: `~/claude-touch/src/twrp-stock-dmesg-20260919.log` |
| Talking to pem120 | `~/claude-touch/ask.sh TAG "Title" "Body" "choice1|choice2|…"` pops a notification plus a zenity dialog on pem120's Hyprland desktop and writes `epoch|phone_uptime|answer` to `~/claude-touch/replies/TAG.txt`. He has answered each prompt within about 35 s. |

## Phone state right now
- (Superseded: mainline is now booted and E09 is loaded. See "E09 results" at the end.) Earlier: in **lk2nd fastboot** (`lk2nd-msm8916`, 23.1-next, unlocked). It landed there
  after `adb reboot` from rev3 TWRP instead of booting extlinux/mainline.
- `fastboot continue` printed "Resuming boot", but lk2nd's log
  (`~/claude-touch/boot/lk2nd-log-1.bin`) shows the command never arrived
  (USB `usb_write() transaction failed … fastboot: oops!`). The device
  re-enumerated and fastboot responds again (`getvar product` OK).
- **Next action:** `fastboot -s 124ccc8f continue` again, then wait for ping to
  `172.16.42.1` and SSH as `ishu` (keys are set up on the laptop; sudo password
  from pem120). If it keeps stopping in fastboot, find out why lk2nd isn't
  auto-booting: reboot reason, a held key, or extlinux. `fastboot boot` of a
  mainline image is a fallback, but `/boot` on the phone is pmOS extlinux, so a
  boot.img would have to be built.
- The power key is broken. Recovery is software-only (or pem120 shorting the
  jumpers). Don't leave the phone somewhere it can't get out of by software.
- rev3 image (sha256 `e350af22…`) must be rebuilt if needed: `build_rev3.py`
  with `~/Downloads/TWRP-3.0.3.0-20170205-ferrari.img`. Extract the FDT at
  offset 27602944 (size 233328), then `dtc` it back to a dts (the E06
  procedure; the orig dts sha256 is `384bd097…`).

## What the controller actually does (E02/E03/E07/E08, raw data in ~/claude-touch)
- Info block `a4 15 21 aa 18 0e 29` (41 objects). The table is intact; info CRC `0x8DDD0D` is correct
  **when read on its own** at `0x00fd`. Reading `7..255` in one transfer returns the table
  plus a bad CRC tail `00 00 fc` (that's the old mainline "Info Block CRC error").
- Reads use a **flat 256-byte map indexed by the low address byte**; the high byte is ignored,
  except at a few exact start addresses: `0x0007` object table, `0x0182` T44, `0x0183` T5.
  Identical on the vendor msm-v2 and mainline QUP masters. The vendor driver sees the same
  map and still works (its `rev_id=2` is flat[0x15], its "Config info 23 01…" is flat[0xde]).
- Single-byte and multi-byte reads can differ: T7 reads `20 09 19` byte by byte, but a
  multi-byte read at 0x8e returns `20 09 40 19…`. **Match the vendor's single-byte
  T7/T6 accesses.**
- Until the chip is started, T5 returns a **static** boot batch (T6 RESET/CRC 0x55571E …
  T109 `05 00`, T6 CAL) and CHG stays high. The vendor driver sees the same batch at probe,
  and its logs also show `mxt_wait_for_chg() timeout!`, so this is normal for this chip.
- The stock TWRP kernel has the biel/tpk/wintek configs **built in**, so stock TWRP always
  uploads biel (CRC 0x164865) plus BACKUPNV plus reset. The chip still reports 0x55571E
  afterwards: its BACKUPNV doesn't persist, or the CRC is canned. MIUI (config
  0x55571E) did no upload.

## Touch tests with pem120 (answers in ~/claude-touch/E08/replies)
| test | setup | result |
|---|---|---|
| t1 | guarded rev2 TWRP, vendor probe replay only (T6 DIAG 0x80/0x81, REPORTALL), read-only CHG monitor | confirmed touches, **0 CHG transitions** |
| t2 | **rev3** (stock driver, no config) | **works**: 4316 evdev events, 10 touch-downs, X 5–983 / Y 12–1894, smooth drags, IRQ count 986 |
| t3 | rev3 after one fb blank→unblank | **works** (IRQ 1441, 455 T100 msgs); **zero T97 key messages** despite firm key taps |

### The stock driver's runtime sequence (debug_enable=1, `E08/rev3/dmesg-rev3-now.txt`)
Single-byte writes, framing `[lo hi val]`:
- **suspend (fb blank):** T7 `0x38e=0 0x38f=0 0x390=0` (IDLE, ACTV, ACTV2IDLE) · T97 CTRL `0x614=0 0x61e=0 0x628=0` · T19 `0x3a7=0xff`
- **resume (fb unblank):** T97 CTRL ×3 `=3` · T19 `0x3a7=0` · T7 `0x390=0x19 0x38f=0x09 0x38e=0x20` · after 100 ms T6 CALIBRATE `0x190=1` (poll until 0)
- Result: an IRQ with a well-formed T6 `01 10 ff b0 76` (CAL, live CRC 0x76B0FF), then normal T100 messages
  through the standard T44+T5 read (`0x182`, 11 bytes, then (count−1)×10 from `0x183`).
- On rev3 the first IRQ came right after TWRP started (its startup blank/unblank). Before that,
  probe issued only T6 DIAG/REPORTALL.

## Why mainline fails (and what DS's tree gets wrong)
- `atmel,write-quirk` (DT) and `mxt_write_quirk` (code, which writes to reg−1 with a dummy 0) come from a
  misread experiment: the evidence matches plain writes where flat[reg−1] already held 0.
  The vendor uses plain writes, and its 09-19 config upload worked. **Drop it.**
- Upstream reads the table and CRC in one transfer, gets the bad CRC and fails with −EIO (pem120
  commented that check out). **Fix: read the CRC separately** (vendor does this).
- Upstream `mxt_start()` writes T7 as a 2-byte block with the values it already has, plus CALIBRATE.
  There is never a deep-sleep→run transition, and the writes aren't single-byte like the vendor's.
  Leading hypothesis: the chip needs the vendor-style T7 0→run cycle and/or CALIBRATE and/or the
  T97/T19 writes. **E09 bisects this.**
- `maxtouch.cfg` (= biel v0x22, sha256 `06f0d07e…`) must stay **absent** from the rootfs. It is hidden at
  `/lib/firmware/e04-backup-20260831-232441/maxtouch.cfg.hidden`. With it, upstream would
  upload plus BACKUPNV plus reset on every boot.
- DS's tree also carries hacks that should not survive: forced RETRIGEN, T44 disabling, info-CRC
  "repair" **write**, `zero_config` sysfs (zeroes config plus BACKUPNV!), `mxt_block_write` param,
  i2c-qup forced DMA / divider changes (their necessity is unproven), and the ported vendor driver `atmel_mxt_ts_336t.c`.
- Chip history: on 09-22/23 DS did many BACKUPNVs, config uploads, T18/T38 writes and bootloader-entry
  attempts (RESET=0xA5). rev3 shows none of it did lasting harm.

## E09 — ready to run (`~/claude-touch/E09`, this dir `e09.patch`)
Upstream 7.0 driver (`c7866ee0a9dd` version, sha256 `dd0476cd…`) plus:
1. split info read (table, then 3 CRC bytes);
2. **test harness only:** `e09_seq` sysfs (write a bitmask) with single-byte, vendor-order writes:
   `0x01` T7=0,0,0 · `0x08` T97 CTRL×3=3 + T19[3]=0 · `0x02` T7 restore (values read at init) ·
   `0x04` +100 ms CALIBRATE and wait · `0x10` REPORTALL · `0x20`/`0x40` upstream 2-byte T7 run/deep-sleep.
Built `atmel_mxt_ts.ko` sha256 `3059b0e3…`, vermagic `7.0.0-msm8916 SMP preempt mod_unload aarch64` (matches the phone).

Plan:
1. Boot mainline. Check `lsmod`, that `/lib/firmware/maxtouch.cfg` is absent, and the IRQ count.
2. `rmmod atmel_mxt_ts; insmod E09/atmel_mxt_ts.ko`. dmesg should show no CRC error and `E09: T7 bytes 20 09 19`.
3. Start `evgrab` (static musl build: `~/claude-touch/E08/evgrab`) on the maXTouch event node,
   `evgrab /dev/input/eventN 900 out.jsonl`. It EVIOCGRABs the node, so the UI won't react.
4. `ask.sh` pem120: **test A** is plain upstream behaviour. If there's no touch, `echo 0x0f > …/e09_seq` and run **test B**.
   If B works, bisect: `0x07` (T7 cycle + cal), `0x03`, `0x04`, `0x08`, then the upstream block variants `0x60|0x04`.
5. Write the minimal patch from the result.

## Target patch series (goal gate 5)
- `Input: atmel_mxt_ts - read info block CRC separately` (generic, harmless on real chips).
- `Input: atmel_mxt_ts - <start sequence fix from E09>`: probably a vendor-style T7 deep-sleep→run
  cycle (single-byte) plus calibrate at start, behind a compatible/DT flag if it isn't generic.
- DTS: drop `atmel,write-quirk`. Keep the regulators (GPIO9 ldo, GPIO78 vdd, L6), level-low IRQ GPIO13, reset GPIO12.
- Revert DS's hacks (list above). Re-test with upstream i2c-qup if possible.
- Commit in DS's checkout (after backing up its WIP) **and** on samcday/linux
  `claude/magical-goodall-comuga` (upstream 7.3-rc4; the ferrari DTS doesn't exist there, so it
  gets the driver patch(es) plus the report). The CRC split applies to 7.3's `mxt_read_info_block()` unchanged in spirit.

## Goal gates (all on mainline, evidence saved on the host)
1. Real finger evdev events plus pem120 confirmation: **not yet**. Works on rev3; on mainline the chip reports (IRQs follow touches) after the E09 sequence, but evdev is unverified
2. 10 min normal use, no I2C errors/IRQ storm/stuck contacts: not yet
3. Survives 3 boots: not yet
4. Survives 3 blank/unblank or suspend/resume cycles: not yet (1 cycle OK on rev3)
5. Minimal reviewed patch series committed in both trees with REPORT.md: not yet

## Authorisation (from Sam)
- **Allowed:** source edits and builds; reversible runtime tests, including temporary T6 commands and RAM-only
  register writes; RAM boots of guarded images; controlled software reboots; test-kernel deploys that
  keep recovery working.
- **Needs Sam's explicit OK:** BACKUPNV or any permanent write, touchscreen firmware or bootloader
  entry (incl. RESET=0xA5), booting unguarded stock TWRP. The one pre-approved stock-TWRP run is **not
  needed**, because rev3 proved no upload is required.

## Gotchas
- The OpenCode `/shell` command is a single argv. Payloads over ~128 KB fail with HTTP 500, so upload in chunks (`rput`).
- The Funnel link drops for minutes at a time (proxy CONNECT 502 / TLS EOF). Retry only on connect-phase errors.
- Keep all artifacts under `~/claude-touch` (persistent), never `/tmp`.
- TWRP 3.10 kernel: glibc static binaries log unknown-syscall dumps (rseq/getrandom); build with
  `aarch64-linux-musl-gcc -static -idirafter /usr/aarch64-linux-gnu/include`.
- Never `pkill -f` a pattern that appears in your own adb command line.
- The vendor driver disables touch on fb blank. Keep the fb unblanked (`echo 0 > /sys/class/graphics/fb0/blank`) during TWRP tests.
## E09 results on mainline (2026-09-24 ~12:10–12:25 UTC, phone uptime 146–536 s)
Phone booted mainline via lk2nd `fastboot continue` (the first attempt was lost to a USB error; the second worked).
DS's quirk module (srcversion FF654C…, mxt_write_quirk=Y) was replaced with E09 `atmel_mxt_ts.ko` (sha256 3059b0e3…).

- Probe: **no info-CRC error** (split read works), `E09: T7 bytes 20 09 19`, input2/event1
  registered, no maxtouch.cfg (-2). Before activation the IRQ fires a few times with
  `T44 count 153 exceeded max report id` / `Unexpected invalid message` (same as rev3's first IRQ).
- **Test A** (plain upstream start path, pem120 confirmed touches, epoch 1790251850–877):
  IRQ count stayed at **6** → the chip does not report under plain upstream.
- `echo 0x0f > /sys/bus/i2c/devices/1-004a/e09_seq` at 259.8 s (T7 0/0/0 → T97 CTRL=3 ×3 + T19[3]=0
  → T7 restore 19/09/20 → +100 ms CALIBRATE): `done err 0`, IRQ 6→8, no invalid-message warnings.
- Test B (dialog closed without an answer, epoch 1790251936): IRQ 8→207 by 294 s, 524 by 415 s.
- **Test B2** (pem120 confirmed, uptime 506.6–535.7 s, epoch 1790252170–189): IRQ **524 → 716**
  during the touches, and **0 IRQs in a 5 s idle window** → **after the vendor-style T7 cycle and
  calibrate, the chip reports touches on mainline.**
- **Caveat:** `evgrab` (started via ssh + sudo + setsid) died early without an end record, so the "0 evdev
  events" for A/B is NOT evidence. Whether the upstream driver turns these messages into input
  events is still **unknown**. There were no T44/invalid-message warnings after activation.
- **Blocked:** everything further needs root on the phone (reading /dev/input/event1 is root:input 0660,
  plus insmod/rmmod, e09_seq, dynamic debug). The phone's sudo needs a password, and Claude's safety
  checks refuse to handle that password. Needs Sam's decision (see HANDOVER).

Next once root is available: start evgrab as a detached root service, enable
`dyndbg` for atmel_mxt_ts, re-run B, then bisect (reload the module between runs to return the chip to its
unstarted state): 0x07, 0x03, 0x04, 0x08, 0x60|0x04 (upstream block writes).

### Mainline touch confirmed by pem120 (E09 module + e09_seq=0x0f)
- With the logger dead, pem120's touches reached the greeter (greetd UI). Sam relayed "touch is working".
- Dialog `replies/e09-confirm.txt`: epoch 1790252483→1790252535, answer
  **"Works well - taps land where my finger is, drags are smooth"**; IRQ 1598 → 2005 during ~55 s of use
  (phone uptime 819→875 s). There are no atmel warnings/errors in dmesg since the 259.8 s e09_seq.
- So: **upstream driver + split CRC read + one vendor-style start sequence
  (T7 0/0/0 → [T97/T19] → T7 restore → CALIBRATE) = working touch on mainline.** The write quirk is not needed.
- Still missing for gate 1: an evdev log (needs root). Gates 2–5 need the bisect, a real patch
  and a persistent install (all need root on the phone).
