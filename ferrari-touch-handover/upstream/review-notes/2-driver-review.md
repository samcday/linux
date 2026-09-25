Both driver patches can realistically go upstream, but they have different odds. Patch 1 (the separate checksum read) is small and changes nothing for genuine chips, so it is likely to be accepted. Patch 3 (the T97 enable), together with its binding patch, will probably need changes and a DT maintainer's ack. The biggest practical risk for either is that the patches get no reply, so pem120 needs to be patient and send pings.

## Patch 1 – 210b841585 "read the info block checksum separately"

**Safe for genuine maXTouch chips.**
- **Same bytes read.** `krealloc` sizes the buffer to 7 + N·6 + 3. The two reads fill [7, 7+N·6) and [size-3, size), which are contiguous, so no byte is left unread (mxt.c:1895-1906, patched file at 536df0e49b). The CRC check is unchanged (mxt.c:1920).
- **Transfer size.** The object-table read is 3 bytes shorter than before. The largest read is still N·6 bytes, at most 1530. Adapters with a `max_read_len` limit are no worse off; they were already a known, unsolved problem (Marek Vasut's 2020 patch, pinged in 2024, never merged: https://lore.kernel.org/linux-input/Zy2q1Ar9BzecljDo@google.com/).
- **Other paths unaffected.** Bootloader mode uses another I2C address, so it fails the same way as before (`mxt_initialize` fallback, mxt.c:2259+). `mxt_update_cfg` only uses the resulting `info_crc`.
- **Cost:** one extra ~7-byte I2C transaction at probe and after a firmware update. That is negligible.
- **Clone trigger:** searching lore for maXTouch together with clone, fake, counterfeit or emulated found no precedent either way. Because the change is neutral for genuine parts, I expect Dmitry to accept it. The commit message should include the evidence from EVIDENCE.md §1 (the flat 256-byte read map, and the same behaviour on the i2c-msm-v2 and QUP masters). Otherwise he may suspect the I2C host controller.
  - Caveat: the tested 7.0 kernel carried a non-upstream i2c-qup forced-DMA hack. Its revert was never verified (REPORT.md "Not included").
- **Build:** I built it for arm64 (clang, W=1, T37 enabled) with no warnings. checkpatch `--strict` only reports the missing Signed-off-by and the non-standard Co-Authored-By trailer.

**Likelihood:** about 60% accepted as-is or with wording tweaks, about 15% with small code changes, about 25% stalls. These are my judgement.

**Changes likely to be requested:**
- Describe the controller more precisely.
- Maybe reject `object_num == 0` outright instead of skipping the read.
- Maybe re-read the checksum only when the CRC check fails.

## Binding (4ffb88519f) and patch 3 (536df0e49b) – T97 enable

**Placement is correct.**
- In deep-sleep mode the write comes before the T7 restore and CALIBRATE (the vendor order).
- In T9 mode it comes after `mxt_soft_reset`, which would otherwise wipe it (mxt.c:3109-3133).
- The T9 branch is effectively dead for DT users: T9 mode is only chosen for DMI-matched Chromebooks (mxt.c:3296).
- Enable-only can't leak key reports in deep sleep, because T7 = 0 stops scanning.

**Gaps a reviewer (or sashiko-bot, the AI reviewer now on linux-input) is likely to raise:**
1. **Controller self-reset.** The T6 RESET handler only completes the reset wait (mxt.c:787), so after a spontaneous reset touch stays dead until the screen is turned off and on again. The commit message admits this, which invites "please handle it". Upstream re-applies nothing after reset for genuine chips either, which is a defensible answer.
2. **Suspend/resume not tested.** `mxt_suspend`/`mxt_resume` call the same `mxt_stop`/`mxt_start` as input inhibit (mxt.c:3407-3443), so the code path was exercised. Real system suspend with rail or IRQ behaviour was not. Test this before sending.
3. **Asymmetry with the vendor driver.** The vendor clears T97 on suspend (EVIDENCE §4); the patch never disables it.
4. **Blind write of 0x03.** It overwrites the other CTRL bits. This is justified only by "the register can't be read back".
5. **Tested on 7.0, not on the 7.3 series as submitted.** Say so in the patch notes.

**Shape: opt-in is right.**
- Enabling T97 unconditionally would change behaviour on in-tree boards with T97 keys (msm8939-longcheer-l9100, msm8916-samsung-gt510, msm8996pro-xiaomi-scorpio) and on genuine configs that disable T97 on purpose.
- Detecting the chip from its info-block ID can't work: it reports exactly the same ID as a genuine mXT336T 2.1.AA.
- The DT maintainers (Rob, Krzysztof, Conor) are the main risk. `enable-t97` is an instruction to the driver, not a hardware description, and nothing in-tree uses it (upstream has no ferrari DTS, and none has been posted on lore). Expect "describe the hardware" or "use a specific compatible". A specific compatible needs the real IC identified, so a photo of the chip marking would help.
- Expect Nick Dyer (the mxt-app author) or Dmitry to ask why the NV config isn't fixed once with a T97 write + BACKUPNV. The config-CRC argument (always 0x55571E) covers `maxtouch.cfg`, but a one-time manual write + BACKUPNV was never tried. **Unverified.**

**Likelihood:** about 15% accepted as-is, about 45% after 1–3 revisions, about 40% stalls. Most likely requests, in order:
1. Rename or re-describe the property, or switch to a compatible.
2. Justify in the commit message why the config can't be fixed in NVM.
3. Re-apply T97 after a T6 reset.
4. Proof of real suspend testing.
5. Disable T97 in `mxt_stop`.

## Precedent and who reviews
- **Dmitry Torokhov is the de facto maintainer and active.** He posted cleanups and fixes in 2026 (d911a55b29bc, a5fd88a5d63f, baa0210fb6a9) and reviews closely (poweroff-sleep review: https://lore.kernel.org/linux-input/Zqg6tNbCn3W79Li_@google.com/).
- **Nick Dyer is listed but mostly inactive:** last commit 74d905d2d38a (2020), one lore message since 2021 (Jan 2025).
- **Other reviewers:** Linus Walleij (binding co-maintainer) and Claudiu Beznea (Microchip) reviewed the 2023 T97 keys patch.

| Series | Timeline | Outcome |
|---|---|---|
| Apitzsch T97 keys | v1 Feb 2023 → v2 → RESEND | Applied May 2023, ~9 weeks (https://lore.kernel.org/linux-input/ZFCCPcKb9xaBZQee@google.com/) |
| Ryhel generic touchscreen props | v1 Sep 3 2025 → v2 | Applied ~3 weeks later |
| Vasut reset GPIO fix | Oct 5 2025 | Applied Oct 10 2025 |
| Eichenberger `atmel,poweroff-sleep` | v1 → v6 (Dec 2023 – Jul 2024), Krzysztof R-b on binding | Never merged |
| Noack per-machine config | v1 Mar 2026 → v3 May 2026 | No human reply, not merged |
| Vasut KoD knob | Dec 2024 | Not merged |

- **AI assistance is accepted in practice.** Dmitry applies patches carrying `Assisted-by:` (e.g. https://lore.kernel.org/linux-input/aqdMplrLuOrSU08z@google.com/).

## What pem120 must do before sending
- Make pem120 the author and add their own Signed-off-by.
- Replace the Co-Authored-By and Claude-Session trailers with `Assisted-by: LLM` (Documentation/process/coding-assistants.rst:48-59).
- Sign off with a "known identity": submitting-patches.rst:439 says no anonymous contributions, and a bare handle like "pem120" may be challenged.
- Send patch 1 on its own first; it is the easy win.
- Cc devicetree@ and the DT maintainers (per get_maintainer.pl) on the binding and T97 patches.

**Not verified:** how maintainers will actually react (the percentages are my judgement), the vendor driver source, dt_binding_check (I didn't re-run it; REPORT.md says it passes), and whether BACKUPNV persists on this chip.

Files are in /tmp/claude-0/-home-user-linux/33841751-cb18-58d1-b467-70ec7733370c/scratchpad/upstream-review/driver-review/:
- patches/ – the formatted patches
- mxt.c – patched driver snapshot
- build/ – arm64 W=1 build
- lore/ – saved threads