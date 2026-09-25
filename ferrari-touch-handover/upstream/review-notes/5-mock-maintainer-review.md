## Mock review panel: atmel_mxt_ts T97 + CRC series (as posted by pem120)

**Can pem120 get this through?** Patch 1 has a real chance, but only after he answers one hard question. Patches 2 and 3 will not be merged in their current form. The most likely way the series fails is pem120 not being able to defend it, not a problem in the code. generated-content.rst:95 lets maintainers "ask the submitter to explain in more detail", and :104-108 says maintainers "are entitled to reject your series without detailed review" if he can't. In HANDOVER.md:39 and :80-85, pem120's role is the tester who answers dialogs; he wasn't the investigator.

### (a) Dmitry Torokhov on patch 1 (210b841585)
> > The maXTouch-compatible controller fitted to … replacement panels
>
> What is this part, actually? The info block is read in one go on purpose. 068bdb67ef74 says the contiguous read lets us "make sure that we are indeed talking to a chip that supports object protocol correctly". Your chip doesn't: by your own description its register map repeats every 256 bytes. I'm not keen to weaken that check for every device so that a clone can bind. If we do it at all, re-read the CRC only after a mismatch, and say explicitly that this is non-Atmel silicon. And was this with a stock i2c-qup?

I verified the 068bdb67ef74 message on git.kernel.org. Neither the hygiene report nor the driver review mentions this stated purpose.

### (a) Dmitry on patch 3 (536df0e49b)
> > +	u16 T97_address;
>
> We already have mxt_write_object() (mxt.c:805-816, already used at :3120). Use it and drop the new field.
>
> > CTRL = ENABLE | RPTEN
>
> There are no keycodes, so why turn on reporting? As far as I know maXTouch keeps its config in NVRAM (<ZnOCDaetFnsg09if@google.com>, verified). Why not fix the config once with mxt-app and BACKUPNV? If the chip resets itself, touch stays dead until reopen (mxt.c:786 only completes the wait), so either handle it or explain why not. This was tested on 7.0 with an out-of-tree QUP change. Please test *this* series, including real system suspend. The binding needs DT acks before I can take this.

### (b) Krzysztof Kozlowski / Rob Herring on patch 2 (4ffb88519f)
> > Enable the T97 … whenever the device is started
>
> You described the desired Linux feature or behavior, not the actual hardware. (This is his verbatim wording from the poweroff-sleep review, in the saved mbox.)
>
> Your patch 1 says this is not a maXTouch. A different device with different bugs gets its own compatible (writing-bindings.rst:68). Don't add properties to avoid a specific compatible (:85-86). Where is the DTS user?

Conor would likely add the question from the same thread: "why can't we … on all systems with this device". The hobbyist goodix precedent ended in "NAK till you answer them" (jajadekroon mbox, verified).

### Blocking objections
1. **Patch 2:** the property describes driver behaviour and stands in for a compatible (writing-bindings.rst:21-22, :85-86). Patch 3 can't go in until this is resolved.
2. **Patches 1 and 3: the device is not a maXTouch.** Its reads use a flat 256-byte map indexed by the low address byte (HANDOVER.md:65-71). Writes land in the same map (E08-REPORT-part1.md:52-54). The CRC check was designed to reject exactly this kind of device (068bdb67ef74). Without a chip identity there is no good compatible, and the cover letter has to take this on directly.
3. **Submitter competence:** see generated-content.rst:104-108 above.

### Likely requested changes
- Patch 3:
  - Use mxt_write_object().
  - Call T97 "PTC Key Set", the datasheet name (ds336t.txt:1790; enum `MXT_TOUCH_PTC_KEYS_T97`).
  - Drop "(config CRC 0x55571E)". The live CRC after start is **0x76B0FF** (E08-REPORT-part1.md:89, HANDOVER.md:91); 0x55571E only appears in the static boot batch (HANDOVER.md:72).
  - Fix "with the upstream driver". With the unmodified driver, probe fails before touch is ever reached.
  - Test whether ENABLE (0x01) without RPTEN is enough.
  - Handle a T6 reset.
- Patch 1: name the part and cite 068bdb67ef74. The fallback only-on-mismatch version is the likely ask.
- Patch 2: replace it with a specific compatible, or reword it as a hardware condition, or drop it in favour of a driver quirk. Say that the board DTS is out of tree.
- Cover letter: disclose the AI assistance and state the test scope exactly.

### Questions pem120 must answer, and whether the evidence answers them
| Question | Answered? |
|---|---|
| What is the actual IC (chip marking, panel vendor)? | **No.** REPORT.md:4 only says it "identifies itself as" an mXT336T. |
| Is this caused by the I2C controller? | **Partly.** The flat map is identical on msm-v2 and on QUP (E08:17), and the CRC reads correctly at 0x00fd. But the tested kernel carried the QUP hack, and the revert was never verified (REPORT.md "Not included"). |
| Why not write T97 once and BACKUPNV? | **Indirectly.** DS's T38 and config BACKUPNVs did not persist (E08:57-62). T97 + BACKUPNV itself was never tried; it needs Sam's approval (HANDOVER.md:153). |
| Is it needed on stock panels, or only replacement panels? | **No.** Only one unit was tested. |
| Real system suspend/resume? A boot of the 7.3 build? | **No / No.** |
| Is instance 0 enough? Is RPTEN needed? Would a one-time write at probe do? | Instance 0: **yes** (413 IRQs, REPORT.md:48-49). RPTEN and a one-time write: **not tested**. |
| Why would T97 gate T100 at all? | **No.** It is only empirical. Because writes alias, "T97" may really just be flat[0x14]. That is my inference, not verified. |
| Are the capacitive keys dead? | **Contradictory.** The MIUI log shows T97 presses (E08:32-34), but t3 got none (E08:80). |

### Claims in the four reports I think are wrong or overstated
1. **Driver review, patch 1 "~60% as-is; expect Dmitry to accept":** too optimistic, because it ignores 068bdb67ef74's stated purpose.
2. **Task context and patch 3 message, "chip always reports config CRC 0x55571E":** only true for the canned boot batch. The live CRC is 0x76B0FF.
3. **DT report, option C:** keying the quirk on 0x55571E is weaker than presented. MIUI's own config reports 0x55571E "OK" (E08:33), so this detects Xiaomi's config, not the replacement panel. It depends on the canned batch being processed before start.
4. **Driver review, "can't leak key reports in deep sleep, because T7=0 stops scanning":** unverified on this chip. Deep sleep was never measured, and accesses alias.
5. **REPORT.md:57, "keys are dead on this panel":** contradicted by the MIUI log (the DTS report also noted this).
6. **Confirmed:** the tag format `Assisted-by: LLM [TOOLS]` (coding-assistants.rst:48-59). The vendor driver reads the CRC separately (vendor driver `atmel_mxt_ts_336t.pristine.c`:2512-2524) but never validates it. The vendor driver writes T97 ×3 on every resume (:4794-4876). The scratch DTS is stale: it has `linux,keycodes` and no `enable-t97` (`dtsw/.../msm8939-xiaomi-ferrari.dts`:189).

**Not verified:** lore returned 403. The mock replies are extrapolated from verified past wording in the saved mboxes in `upstream-review/dt-binding/`. No new builds or tests were run. My scratch copies are the patched driver snapshot `upstream-review/panel/mxt.c` and the fetched commit `upstream-review/panel/068b.patch`, both under `/tmp/claude-0/-home-user-linux/33841751-cb18-58d1-b467-70ec7733370c/scratchpad/`.