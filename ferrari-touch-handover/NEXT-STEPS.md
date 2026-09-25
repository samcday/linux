# Handover to pem120: Mi 4i touchscreen (2026-09-25)

Claude's part of this work ends here. This page says where everything is and what's left. The details are in
[REPORT.md](REPORT.md) (root cause, fix, evidence) and [upstream/UPSTREAMING.md](upstream/UPSTREAMING.md) (sending the
fixes to LKML). On ishulappy all of this is in `~/claude-touch/docs/`, a copy of `ferrari-touch-handover/` on branch
`claude/magical-goodall-comuga` of github.com/samcday/linux.

## Where things are

### Kernel tree `~/Projects/Android/msm8939/linux`
`msm8939/mi4i` = **`claude/ferrari-touch-rebased`** (9d28c6b714ff): 464923b plus 6 commits, rebased at your request. It is
checked out.

| commit | what |
|---|---|
| c1ffcb40270c leds: lm3533, backlight: lm3533_bl: add missing includes | build fix: **464923b doesn't build** with your config (`leds-lm3533.c`: incomplete `struct of_device_id`). Same two lines as 5d8f10ccdc03; could be squashed into the lm3533 patches |
| 231dfd6898e3 Input: atmel_mxt_ts - restore the info block CRC check | undoes the bring-up change that turned the CRC error into a warning (tree-only, not for upstream) |
| d49f1b3da33f Input: atmel_mxt_ts - read the info block checksum separately | fixes probe (the tested version of upstream patch A) |
| 9ebd65f703bd dt-bindings: input: atmel,maxtouch: add atmel,enable-t97 | binding (the tested version of upstream patch B1) |
| b64fb0cfc76d Input: atmel_mxt_ts - optionally enable the T97 key array on start | makes the chip report touches (the tested version of upstream patch B2) |
| 9d28c6b714ff arm64: dts: qcom: msm8939-xiaomi-ferrari: fix the touchscreen | the tested touchscreen node, with `atmel,enable-t97` |

- 464923b already has stock i2c-qup, so the i2c-qup changes (5d8f10ccdc03 and DS's uncommitted edits) are simply not
  on this branch. The vendor `atmel_mxt_ts_336t` driver isn't either.
- The driver, binding, DTS and `i2c-qup.c` are **byte-identical** to the tree you tested as kernel #9. The only other
  difference from #9 is the removed 336t driver, which the DTS no longer uses.
- **Build check (2026-09-25):**
  - Setup: your `.config`, host Debian clang 19, a separate build dir that has since been deleted.
  - Result: lm3533 (mfd/leds/backlight), i2c-qup, atmel_mxt_ts and the DTB all compile.
  - The DTB is **byte-identical** to the one in your #9 build (sha256 4b9401cd…).
  - The full kernel was **not** built or booted from this branch. That is step 1 below.

Other branches and tags:
- `claude/ferrari-touch-stock-qup` (9988b32e8fd8): what you built and tested as kernel #9 (the series + i2c-qup
  revert, on 03fc2dcd).
- `claude/ferrari-touch` (0ad6ff8e5f37): the series on 03fc2dcd without the revert.
  - The 3-boot, 10-minute and blank/unblank gates ran on kernel #8. That was DS's kernel build from 03fc2dcd plus DS's
    uncommitted WIP (including its i2c-qup.c).
  - Only the touch module and DTB came from this branch.
- `ds-wip-2026-09-24`: DS's uncommitted changes from 2026-09-24 (i2c-qup.c, atmel_mxt_ts.c, atmel_mxt_ts_336t.c, DTS),
  saved as a commit. Also `~/claude-touch/ds-wip-2026-09-24/ds-wip.diff`.
- Tag `mi4i-before-rebase` = 03fc2dcd, DS's old `msm8939/mi4i` head (the 336t port and 5d8f10ccdc03). It keeps that
  history even if you delete the branches above.
- `claude/ferrari-touch-v1` (an older, rejected design) and `claude/qup-revert-untested` (superseded): safe to delete.

**Signing and trailers.** Your global git config signs every commit.
- Commits Claude made on 2026-09-24/25 while your gpg-agent had the passphrase cached **carry your signature**. Confirmed
  for 0ad6ff8e5f37 (tip of `claude/ferrari-touch`) and 9988b32e8fd8 (`claude/ferrari-touch-stock-qup`).
- The 6 rebased commits are **unsigned**. The first attempt popped a pinentry dialog on your desktop, which timed out.
- They carry `Co-Authored-By: Claude` / `Claude-Session:` trailers.
- **For LKML**, don't use these commits. Use the reworked v1 patches in `docs/upstream/patches/`, which already have
  `Assisted-by: LLM` (UPSTREAMING.md).
- **For a msm8916-mainline PR**, review each commit, then sign it, fix the trailers and add your Signed-off-by in one go:
  ```sh
  git rebase 464923b94bbb msm8939/mi4i --exec 'git log -1 --format=%B | sed -e "/^Co-Authored-By:/d" -e "/^Claude-Session:/d" | git interpret-trailers --trailer "Assisted-by: LLM" | git commit -q --amend --reset-author -s -F -'
  ```
  - If it stops (for example pinentry timed out), `git rebase --abort` restores the branch.
  - Afterwards the hashes on this page no longer match `msm8939/mi4i`. `claude/ferrari-touch-rebased` still points at
    the old ones.

### Phone
- Kernel #9 (branch `claude/ferrari-touch-stock-qup`) is **installed** in `/boot` (updated 2026-09-25 11:43–11:45 IST),
  so a restart boots #9. Touch works on it; see REPORT.md, E12.
- No `/lib/firmware/maxtouch.cfg`. **Keep it that way**: with it, the driver would upload the file and write it to NVM
  on every boot. The old config stays parked at `/lib/firmware/e04-backup-20260831-232441/maxtouch.cfg.hidden`.
- Phosh idle-delay is back to 300 s.
- Old backups can be deleted when you no longer need them:
  - `/boot/msm8939-xiaomi-ferrari.dtb.pre-claude`
  - `/boot/initramfs.pre-claude`
  - `/lib/modules/7.0.0-msm8916/kernel/drivers/input/touchscreen/atmel_mxt_ts.ko.pre-claude`
  - the older `/boot/msm8939-xiaomi-ferrari.*.bak`
- Test logs are in `/home/ishu/claude-e09/`.

**Permanent writes to the touch controller need Sam's OK.** That covers:
- BACKUPNV (including via `mxt-app`);
- writing to `/sys/bus/i2c/devices/*-004a/update_fw` (it flashes firmware and enters the bootloader);
- installing any `maxtouch.cfg`;
- booting unmodified stock TWRP (it uploads its built-in config, then BACKUPNV and a reset, on every boot).

### Laptop `~/claude-touch/`
- Evidence per experiment: E02–E12.
- `docs/`: this page, the report and the upstreaming plan.
- `rescue/rescue-boot.img`: the pre-Claude kernel, DTB and initramfs. Use it only as a last resort (`fastboot boot` from
  lk2nd, RAM only).
- `tools/`, `ask.sh`, `replies/`, `ft-docs-*.tgz`: Claude's helpers and leftovers. No longer needed.
- Claude's git worktree and build dirs have been removed. `git worktree list` shows only your checkout.

## What to do next
1. **Build and boot `msm8939/mi4i`** once.
   - Build it the way you built #9 (`pmbootstrap build --envkernel ...`), not with the host clang: the only kernel
     that never brought up USB was a host Debian clang 19 build.
   - **RAM-boot it first** from lk2nd (`fastboot boot`, e.g. `pmbootstrap flasher boot`). `/home/ishu/rebootbl` on the
     phone reboots into lk2nd.
   - If it hangs, a jumper restart brings back the installed #9. Install it only once it works.
   - Check:
     - `dmesg | grep -i -E 'mxt|maxtouch'` shows `Family: 164 Variant: 21 Firmware V2.1.AA Objects: 41` and no CRC
       error. One or two `T44 count 153 exceeded max report id` / `Unexpected invalid message` lines at probe are
       normal.
     - Taps and drags work (`evtest` over SSH if you want numbers).
     - Touch still works after an input inhibit cycle. This is how the #8 gate did blank/unblank:
       ```sh
       N=$(grep -l maXTouch /sys/class/input/input*/name | grep -o 'input[0-9]*')
       echo 1 | sudo tee /sys/class/input/$N/inhibited; sleep 3; echo 0 | sudo tee /sys/class/input/$N/inhibited
       ```
       A real screen blank can leave the display dark: that's a separate mdp5 issue, see REPORT.md.
2. **Remaining tests before LKML**: see "Still open" below and UPSTREAMING.md §5, which explains how to make the
   test variants from `msm8939/mi4i`.
3. **Send to LKML** following UPSTREAMING.md: patch A alone first, then B, with the cover-letter text given there.
   - Expect slow replies, and answer them yourself.
   - If you'd rather not be the public contact for months, tell Sam before sending.
4. **Later:** the board DTS for msm8916-mainline, then linux-arm-msm (UPSTREAMING.md §11).

## Still open (nobody has checked these)
- On stock i2c-qup, the exact `Info Block CRC error` line from a build **without** patch A, and an A-only build
  (probes, no touches). These are UPSTREAMING.md §5 steps 1–2.
- 3 boots, 10 minutes of use and 3 blank/unblank cycles on stock i2c-qup. They passed on #8 (with DS's i2c-qup
  change); #9 was booted once and only taps were checked.
- A 7.3-based kernel (there is no ferrari DTS for 7.3 yet; see UPSTREAMING.md §5).
- Real system suspend (`rtcwake`, jumpers ready).
- The chip marking.
- Why the capacitive keys never produced T97 messages in our tests although MIUI logged key events.

## Housekeeping
- The OpenCode server gives a shell as `ishu` to anyone with its URL and password.
  - When you're done with remote help, stop it (or change its password) and turn off its Funnel.
  - Check `tailscale funnel status` first: `tailscale funnel reset` removes **all** serve/funnel entries.
- The Claude sessions in OpenCode ("claude-shell" and the helper used for shell calls) can be deleted.
