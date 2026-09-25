# Upstreaming the Xiaomi Mi 4i (ferrari) touchscreen fixes

Owner: pem120. Written 2026-09-25. Nothing has been sent yet.

This plan came from a review of the series: patch hygiene, driver design, the DT binding, the board-DTS path, and an
adversarial "mock maintainer" review. The reports are in [`review-notes/`](review-notes/). Paths inside those notes
that start with `/tmp/claude-0/...` pointed at a temporary workspace that no longer exists. Every file you need is in
this directory.

## 1. What gets sent, and in what order

| step | what | to | chance (estimate) |
|---|---|---|---|
| A | `Input: atmel_mxt_ts - read the info block checksum separately` (1 patch) | Dmitry Torokhov, linux-input | good (~60–70 %). The main risk is no reply, not rejection |
| B | `dt-bindings: input: atmel,maxtouch: add atmel,enable-t97` + `Input: atmel_mxt_ts - optionally enable T97 on start` (2 patches + cover letter) | Dmitry, DT maintainers, linux-input, devicetree | uncertain (~35–50 %). The DT maintainers may object to the property, and it has no in-tree user |
| C (later, optional) | board DTS `msm8939-xiaomi-ferrari.dts` | Qualcomm SoC maintainers, linux-arm-msm (msm8916-mainline first) | separate project |

With A alone the driver probes. Touch needs A and B. B applies without A.

The v1 patches in [`patches/`](patches/) apply to mainline fe2ec83746 (v7.3-rc4 + 2 days). They also apply unchanged to
dtor/input `next` and next-20260924: nothing touched these two files upstream since rc4.
- `A-0001-*.patch`: A
- `B-0001-*.patch`, `B-0002-*.patch`: B (write the cover letter from §7)
- `mxt-v1.bundle`: the same commits as a git bundle, with branches `mxt-crc` (A) and `mxt-t97` (B)

They are reworked from the 7.3 commits 210b841585, 4ffb88519f and 536df0e49b. The phone ran their 7.0 backport, which in
DS's tree is now d49f1b3da33f, 9ebd65f703bd and b64fb0cfc76d on `msm8939/mi4i`:
- The author is a placeholder. Co-Authored-By / Claude-Session are replaced by `Assisted-by: LLM` (format per
  Documentation/process/coding-assistants.rst). You add your own Signed-off-by (§4).
- The T97 write uses the existing `mxt_write_object()` helper, and the extra `T97_address` field is gone. The I2C write
  is the same one tested on the phone: one byte, 0x03, to T97 instance 0 CTRL (0x614).
- T97 has its datasheet name, "PTC Key Set".
- The commit messages are corrected:
  - they name the device and cite 068bdb67ef74;
  - the unverifiable "config CRC 0x55571E has T97 disabled" is gone;
  - "no touches with the upstream driver" is gone (upstream fails probe first);
  - they make no claims about the capacitive keys.
- The binding text describes the controller, not what the driver should do.

Checks run on these files:
- `checkpatch --strict` is clean apart from the missing Signed-off-by.
- The arm64 clang W=1 build of `atmel_mxt_ts.o` has no warnings.
- `dt_binding_check` passes.

**Not yet done:** none of these v1 files has been booted. The phone ran the equivalent 7.0 backport (§5).

## 2. Gate: can you defend it?
The kernel's rules on AI-assisted patches (Documentation/process/generated-content.rst) say:
- you must be able to understand and defend everything you submit;
- maintainers may reject a series without detailed review if you can't.

Before you send anything:
1. Read Documentation/process/submitting-patches.rst, coding-assistants.rst, generated-content.rst and email-clients.rst.
2. Read the three diffs (~70 lines) and ../REPORT.md.
3. Make sure you can answer every question in §8 in your own words.
4. Be ready to answer review email yourself, within about a week, for 3–6 months.

If any of this doesn't work for you, tell Sam before sending. Sam could submit instead, with your Tested-by.

## 3. Setup (once)
```sh
git clone https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git && cd linux
git config user.name  "Your Name"        # a known identity: the kernel doesn't take anonymous contributions
git config user.email "you@example.org"  # an inbox you will read
git fetch ~/claude-touch/docs/upstream/patches/mxt-v1.bundle mxt-crc:mxt-crc mxt-t97:mxt-t97   # needs fe2ec83746 in the clone
pipx install b4 && pipx install dtschema && pipx install yamllint && pipx ensurepath   # Debian refuses pip --user
git config --global b4.send-endpoint-web https://lkml.kernel.org/_b4_submit
b4 send --web-auth-new              # then: b4 send --web-auth-verify <challenge from the email>
```
With the web endpoint you don't need SMTP. Replies come from your own mail client: plain text, inline replies,
reply-all, no HTML (see email-clients.rst).

## 4. Make the patches yours
```sh
BASE=$(git describe --tags --abbrev=0 origin/master)   # newest mainline tag, e.g. v7.3-rc6; write it down, §6/§7 need it again
for b in mxt-crc mxt-t97; do
  git rebase --onto $BASE fe2ec83746 $b
  git rebase --exec 'git commit --amend --no-edit --reset-author -s' $BASE $b
done
git log --format='%an <%ae>%n%(trailers)' $BASE..mxt-crc $BASE..mxt-t97  # you; Assisted-by: LLM, then your Signed-off-by
./scripts/checkpatch.pl --strict -g $BASE..mxt-crc      # expect 0 errors, 0 warnings
./scripts/checkpatch.pl --strict -g $BASE..mxt-t97
git checkout mxt-t97 && make dt_binding_check DT_SCHEMA_FILES=input/atmel,maxtouch.yaml
make ARCH=arm64 LLVM=1 defconfig && make ARCH=arm64 LLVM=1 W=1 drivers/input/touchscreen/atmel_mxt_ts.o
```
Tags:
- No `Fixes:` and no `Cc: stable`. Patch A works around a controller quirk; it doesn't fix a bug in 068bdb67ef74.
- Add a `Co-developed-by:` + `Signed-off-by:` pair only for another human who co-wrote the patches, and only with
  their consent.

## 5. Test on the phone (required; do not skip)
**Why this matters.** Every mainline test before 2026-09-25 (E12) ran on kernel `7.0.0-msm8916 #8`. That kernel was built from DS's
`msm8939/mi4i` @ 03fc2dcd **plus DS's uncommitted i2c-qup.c changes**. Those changes are saved on branch
`ds-wip-2026-09-24` in DS's checkout. The i2c-qup driver in that kernel therefore differs from upstream twice:
- committed 5d8f10ccdc03: forced DMA and custom SCL dividers;
- the uncommitted change: upstream DMA heuristic, but a custom `fs_div`/`ht_div` for every bus speed.

**Update 2026-09-25 (E12).** Touch works with **stock i2c-qup** on a 7.0-based kernel:
- the build is pem120's kernel #9, branch `claude/ferrari-touch-stock-qup`. `msm8939/mi4i` has the same touch code
  rebased onto 464923b;
- probe OK, no CRC error, no I2C errors;
- 6/6 touch-downs and releases across the panel;
- pem120 confirmed it.
The evidence is in `~/claude-touch/E12-stockqup/` on ishulappy. This answers "is it your I2C controller?" for the
tested combination (A + B).

Still to do from the list below:
- step 1 (the CRC error line without A) and step 2 (A only), on stock i2c-qup;
- 3 boots, blank/unblank and the 10 minutes on stock i2c-qup (#9 was booted once and only taps were checked);
- a 7.3 kernel;
- real suspend.
(An earlier stock-i2c-qup build of mine, made with a different clang, never brought up USB. pem120's pmbootstrap
build boots fine. Keep the jumpers ready anyway.)

**Making the test variants on the 7.0 tree.** Branch from `msm8939/mi4i`, never from 464923b. 464923b's own driver
only warns on the CRC mismatch; 231dfd6898e3 restores the check.
```sh
git switch -c test-no-A  msm8939/mi4i && git revert --no-edit d49f1b3da33f   # step 1: expect the CRC error
git switch -c test-A-only msm8939/mi4i && git revert --no-edit b64fb0cfc76d  # step 2: probes, no touches
```
Build each with pmbootstrap `--envkernel` and RAM-boot it (`fastboot boot`) so the installed kernel stays the way back.

**A 7.3 kernel.** There is no ferrari DTS for 7.3 yet. On msm8916-mainline `wip/msm8916/7.3-rcN`:
1. Apply the three v1 patches (`git am` or merge `mxt-crc` and `mxt-t97`).
2. Copy `msm8939-xiaomi-ferrari.dts` (plus its Makefile line) from `msm8939/mi4i`, not the minimal-UNTESTED DTS, whose
   touch regulators differ.
3. Drop or port the panel and lm3533 nodes. Touch doesn't need the display: test over SSH with `evtest`.

If that is too much work, test the v1 patches backported onto `msm8939/mi4i` instead, and say "7.0-based
msm8916-mainline kernel with the patches backported; not tested on 7.3" in the notes and cover letter.

Use this kernel:
- a 7.3-based msm8916-mainline kernel (`wip/msm8916/7.3-rc2` or newer);
- **stock i2c-qup.c** (identical to upstream);
- the ferrari DTS with `atmel,enable-t97`;
- no `/lib/firmware/maxtouch.cfg`.

For each step, save `uname -a` and `dmesg | grep -i mxt`:
1. **Without A.** Copy the exact `Info Block CRC error calculated=0x… read=0x…` line.
2. **A only.**
   - Probe succeeds: `Family: 164 Variant: 21 Firmware V2.1.AA Objects: 41`.
   - There are no touches: the mxt line in `/proc/interrupts` doesn't grow while you touch the screen.
3. **A + B.** Touch works:
   - after 3 boots;
   - after 3 screen blank/unblank cycles;
   - over about 10 minutes of use (check with `evtest`).
4. **Real suspend.** Run `sudo rtcwake -m freeze -s 20` three times and check touch after each.
   - The power key is broken, so first confirm that rtcwake wakes the phone at all, with the jumpers ready.
   - If you can't do this safely, write "system suspend not tested" in the cover letter.

If a result contradicts the patch text, stop and get the patches revised before sending. In particular:
- if touch fails with stock i2c-qup, the problem is not (only) the controller;
- if probe works without A, drop A.

## 6. Send A
```sh
git checkout mxt-crc && b4 prep -e $BASE
b4 prep --edit-cover     # for a single patch, b4 puts this text below the --- line
b4 prep --auto-to-cc     # expect Dmitry Torokhov, Nick Dyer, linux-input, linux-kernel
b4 prep --check
b4 send -o /tmp/presend && b4 send --reflect   # read what arrives in your inbox
b4 send
```
Notes text:
```
Tested on a Xiaomi Mi 4i (ferrari, msm8939) with an aftermarket replacement
display panel, on <kernel, stock i2c-qup>. Without this patch:
  <exact Info Block CRC error line>
With it the device probes:
  <Family: 164 Variant: 21 ... line>
The controller also needs T97 enabled before it reports touches; I am sending
that as a separate series. Not tested on a genuine maXTouch, which returns
the same bytes either way; a Tested-by from anyone with maXTouch hardware
is welcome.
This patch was written with an AI coding assistant (Claude Code), based on
I2C traces from the device. I ran the tests on the hardware and have
reviewed the change.
```

## 7. Send B (about a week after A, or once A gets its first reply)
Use the same b4 commands on `mxt-t97`. `--auto-to-cc` should add Rob Herring, Krzysztof Kozlowski, Conor Dooley,
Linus Walleij and devicetree. It also adds the AT91 maintainers via an `N: atmel` pattern; that's harmless. Don't mark
the series RFC. Cover letter:
```
Subject: Input: atmel_mxt_ts - enable T97 for a maXTouch-compatible controller

The Xiaomi Mi 4i (ferrari, msm8939) I am bringing up on mainline has an
aftermarket replacement display panel. Its touch controller reports itself
as an mXT336T (family 0xa4, variant 0x15, fw 2.1.AA, 41 objects), but it
looks like firmware emulating the maXTouch protocol: reads outside a few
special addresses return a flat 256-byte map. <chip marking if known>

With the info block fix [1] the driver probes, but the controller reports
no touches until T97 (PTC Key Set) instance 0 is enabled, which the Xiaomi
vendor driver does on every resume. Patch 1 adds a DT property for this and
patch 2 implements it. Boards without the property are not affected.
The ferrari board DTS is not upstream yet: <link to downstream DTS / plan>.

Testing (<kernel>, stock i2c-qup): 3 boots, 3 blank/unblank cycles, about
10 minutes of use. System suspend: <3x rtcwake / not tested>. The T9
control-mode path (some Chromebooks only) is not tested.

The investigation (I2C traces, bisecting the vendor driver's resume
sequence on the device) and the patches were done with an AI coding
assistant (Claude Code). I ran the hardware tests and have reviewed the
patches.
[1] <lore link to A>
```

## 8. Answers to likely review questions
- **What is this chip?**
  - It reports mXT336T IDs, but its register map is flat, so it looks like an emulating controller on an aftermarket
    panel.
  - Give the marking if you can photograph it safely. Otherwise say it is unknown.
- **Is it your I2C controller?**
  - The flat map looks the same with the vendor kernel's i2c-msm-v2 master.
  - Touch works on stock i2c-qup (E12, 2026-09-25). Quote that, and the CRC-error line from §5 step 1 once you have it.
- **068bdb67ef74 wanted the info block read as one block.**
  - That commit already used two transfers: the 7-byte header, then the rest.
  - The CRC is still calculated over the same contiguous buffer and still compared.
  - If the maintainer prefers, offer "re-read the checksum only after a mismatch" for v2.
- **Why not a compatible?**
  - The chip reports the same family, variant and firmware as a genuine mXT336T, and its real part number is unknown.
  - On other boards T97 must be left as their config sets it.
- **Why not reuse linux,keycodes?**
  - That would change behaviour on in-tree boards that already set keycodes (l9100, gt510, mocha and others).
  - The control byte can't be read back for a read-modify-write.
- **Why not fix the config once with BACKUPNV?**
  - Stock TWRP uploads a config with BACKUPNV on every boot, yet the chip keeps reporting the same canned CRC, so a
    BACKUPNV'd config change has never been observed to stick.
  - The vendor driver itself writes T97 at run time.
  - T97 + BACKUPNV has **not** been tried: it is a permanent write and needs Sam's explicit OK.
- **Why RPTEN without keys?** It is the vendor value and the one the bisect tested. With no keycodes, the driver
  ignores T97 messages.
- **What if the chip resets itself?** Not seen in testing. Offer to re-enable T97 on a T6 RESET message in v2.
- **Was this AI-written?** Yes. Say so plainly, and say what you tested.

## 9. After sending
- **Within about a day:** the sashiko-bot AI reviewer may reply on linux-input, and Rob Herring's bot runs
  dt_binding_check. Fix real problems in v2 and reply briefly where you disagree.
- **Humans:** replies often take 1–4 weeks, sometimes months. Other feature patches for this driver sat for months in
  2026.
  - Answer every point, inline, within about a week.
  - Don't send v2 while a discussion is still open.
- **v2:**
  1. Edit the commits.
  2. Run `b4 trailers -u` to pick up Reviewed-by tags.
  3. Run `b4 prep --edit-cover` and add a "Changes in v2" list.
  4. Run `b4 send`.
- **Silence:**
  - After 3 weeks, reply to your own patch with "Gentle ping". Don't ping during a merge window.
  - After the next -rc1, run `b4 send --resend v1`.

## 10. Checkpoints (for you and Sam)
Lore blocked automated access during the review, so these links weren't tried.
1. §5 results written down (target ~2026-10-02).
2. A on lore (~2026-10-05): https://lore.kernel.org/linux-input/?q=s%3A%22read+the+info+block+checksum+separately%22
3. B on lore (~2026-10-12): https://lore.kernel.org/all/?q=s%3A%22atmel%2Cenable-t97%22
4. Every reviewer email answered by pem120 in the thread within about a week.
5. Patch status: https://patchwork.kernel.org/project/linux-input/list/?q=atmel_mxt_ts
6. A DT tag (Reviewed-by or Acked-by from Rob, Krzysztof or Conor) in the binding thread.
7. Dmitry replies "Applied". The commit shows up at https://git.kernel.org/pub/scm/linux/kernel/git/dtor/input.git/log/?h=next
8. It reaches mainline: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/log/drivers/input/touchscreen/atmel_mxt_ts.c
   Estimate: A in 7.4 or 7.5; B in 7.5 or later.

## 11. Later: board DTS (C)
- There is no ferrari DTS upstream or in the msm8916-mainline branches checked (see review-notes/4-board-dts.md). DS's
  downstream DTS has an unsupported panel and lm3533 nodes, a `dr_mode` schema failure and FIXME leftovers.
- [`msm8939-xiaomi-ferrari.minimal-UNTESTED.dts`](msm8939-xiaomi-ferrari.minimal-UNTESTED.dts) is a trimmed starting
  point. According to the review it passes `dtbs_check` W=1 (the only warnings are from msm8939.dtsi).
  - It **has never been booted**.
  - Its touch regulator wiring (`vdd` fed from `reg_ts_ldo`, `vdda` from `pm8916_l6`) differs from the DTS tested on
    the phone (both fixed regulators fed from `pm8916_l6`).
  - The voltages were never measured.
- Order:
  1. Get a PR into msm8916-mainline.
  2. Add a `qcom.yaml` compatible patch and send the DTS to linux-arm-msm. Add `atmel,enable-t97` once the binding is
     in linux-next, which gives the property an in-tree user.

## Not yet verified by anyone
- The CRC-error line without patch A and the A-only run, on stock i2c-qup (§5 steps 1–2). Touch with A + B on stock
  i2c-qup works (E12, one boot, taps only); 3 boots, blank/unblank and 10 minutes were not re-run on it.
- Any 7.3 kernel on this phone.
- Real system suspend/resume.
- The chip marking.
- Whether original, non-replacement ferrari panels need either fix. The vendor driver writes T97 on every resume on all
  units, so enabling it matches stock behaviour.
- Patch A on genuine maXTouch hardware.
- Capacitive keys: they produced no T97 messages in any of our tests (rev3 TWRP, mainline), but the MIUI log shows T97
  key events. The cause is unknown, so the patches make no claims about keys.
