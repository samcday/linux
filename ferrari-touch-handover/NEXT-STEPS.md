# Handover to pem120: Mi 4i touchscreen (2026-09-25)

Claude's part of this work ends here. This page says where everything is and what's left. The details are in
[REPORT.md](REPORT.md) (root cause, fix, evidence) and [upstream/UPSTREAMING.md](upstream/UPSTREAMING.md) (sending the
fixes to LKML). On ishulappy both are in `~/claude-touch/docs/`.

## Where things are

### Kernel tree `~/Projects/Android/msm8939/linux`
`msm8939/mi4i` = **`claude/ferrari-touch-rebased`** (9d28c6b714ff): 464923b plus 6 commits, rebased at your request. It is
checked out.

| commit | what |
|---|---|
| c1ffcb40270c leds: lm3533, backlight: lm3533_bl: add missing includes | build fix: **464923b doesn't build** with your config (`leds-lm3533.c`: incomplete `struct of_device_id`). Same two lines as 5d8f10ccdc03; could be squashed into the lm3533 patches |
| 231dfd6898e3 Input: atmel_mxt_ts - restore the info block CRC check | undoes the bring-up change that turned the CRC error into a warning (tree-only, not for upstream) |
| d49f1b3da33f Input: atmel_mxt_ts - read the info block checksum separately | fixes probe (upstream patch A) |
| 9ebd65f703bd dt-bindings: input: atmel,maxtouch: add atmel,enable-t97 | binding (upstream patch B1) |
| b64fb0cfc76d Input: atmel_mxt_ts - optionally enable the T97 key array on start | makes the chip report touches (upstream patch B2) |
| 9d28c6b714ff arm64: dts: qcom: msm8939-xiaomi-ferrari: fix the touchscreen | the tested touchscreen node, with `atmel,enable-t97` |

- 464923b already has stock i2c-qup, so the i2c-qup changes (5d8f10ccdc03 and DS's uncommitted edits) are simply not
  on this branch. The vendor `atmel_mxt_ts_336t` driver isn't either.
- The driver, binding, DTS and `i2c-qup.c` are **byte-identical** to the tree you tested as kernel #9.
- The only other difference from #9 is the removed 336t driver (the DTS no longer uses it).
- **Build check (2026-09-25):** with your `.config` (host Debian clang 19, separate `O=~/claude-touch/linux-rebase-out`),
  these all compile: lm3533 (mfd/leds/backlight), i2c-qup, atmel_mxt_ts and the DTB. The DTB is **byte-identical** to
  the one in your #9 build (sha256 4b9401cd…). The full kernel was not built or booted from this branch.

Other branches:
- `claude/ferrari-touch-stock-qup`: what you built and tested as kernel #9 (the series + i2c-qup revert on 03fc2dcd).
- `claude/ferrari-touch`: the series on 03fc2dcd without the revert (kernel #8, where the 3-boot, 10-minute and
  blank/unblank gates ran).
- `ds-wip-2026-09-24`: DS's uncommitted changes from 2026-09-24 (i2c-qup.c, atmel_mxt_ts.c, atmel_mxt_ts_336t.c, DTS),
  saved as a commit. Also `~/claude-touch/ds-wip-2026-09-24/ds-wip.diff`.
- `claude/ferrari-touch-v1` (an older, rejected design) and `claude/qup-revert-untested` (superseded): safe to delete.

**Signing and trailers.** Your global git config signs every commit.
- Commits Claude made on 2026-09-24/25 while your gpg-agent had the passphrase cached **carry your signature**, e.g.
  `claude/ferrari-touch` and 9988b32e8fd8.
- The 6 rebased commits are **unsigned**. The first attempt popped a pinentry dialog on your desktop, which timed out.
- To review and sign them yourself: `git rebase --exec 'git commit --amend --no-edit -S' 464923b94bbb msm8939/mi4i`
- They carry `Co-Authored-By: Claude` / `Claude-Session:` trailers. For a msm8916-mainline PR or LKML, replace these
  with `Assisted-by: LLM` and add your own `Signed-off-by:` (see UPSTREAMING.md §4).

### Phone
- It runs your kernel #9 (branch `claude/ferrari-touch-stock-qup`). Touch works; see REPORT.md, E12.
- No `/lib/firmware/maxtouch.cfg`. **Keep it that way**: with it, the driver would upload the file and write it to NVM
  on every boot.
- Phosh idle-delay is back to 300 s.
- Old backups from the E11 deploy can be deleted when you no longer need them:
  - `/boot/msm8939-xiaomi-ferrari.dtb.pre-claude`
  - `/boot/initramfs.pre-claude`
  - `/lib/modules/7.0.0-msm8916/kernel/drivers/input/touchscreen/atmel_mxt_ts.ko.pre-claude`
- Test logs are in `/home/ishu/claude-e09/`.

### Laptop `~/claude-touch/`
Evidence per experiment (E02–E12), `docs/`, `rescue/rescue-boot.img` (RAM-only rescue boot: `fastboot boot` it from
lk2nd) and `tools/`. The Claude tools (`rsh`, `ask.sh`, the `*.sh` scripts) are no longer needed.

## What to do next
1. **Build and boot `msm8939/mi4i`** (the rebased branch) once:
   - `dmesg | grep -i mxt` shows `Family: 164 Variant: 21 ...` and no CRC error;
   - taps and drags work;
   - touch still works after one screen blank/unblank.
2. **Remaining tests before LKML** (UPSTREAMING.md §5): see "Still open" below.
3. **Send to LKML** following UPSTREAMING.md: patch A alone first, then B, with the cover-letter text given there. Use
   the reworked v1 patches in `docs/upstream/patches/`, not these tree commits.
   - Expect slow replies, and answer them yourself.
   - If you'd rather not be the public contact for months, tell Sam before sending.
4. **Later:** the board DTS for msm8916-mainline, then linux-arm-msm (UPSTREAMING.md §11).

## Still open (nobody has checked these)
- The exact `Info Block CRC error` line from a build **without** patch A, on stock i2c-qup (UPSTREAMING.md §5 step 1).
- 10 minutes of use and 3 blank/unblank cycles on stock i2c-qup. They passed on #8 (with DS's i2c-qup change); on #9
  only taps were checked.
- A 7.3-based kernel; real system suspend (`rtcwake`, jumpers ready); the chip marking.
- Why the capacitive keys never produced T97 messages in our tests although MIUI logged key events.

## Housekeeping
- The OpenCode server now has a password. When you're done with remote help, also turn off the Tailscale Funnel for
  it (`tailscale funnel reset`).
- The Claude helper sessions in OpenCode ("claude-shell" and the one used for shell calls) can be deleted.
