# DT binding review: `atmel,enable-t97` (4ffb88519f)

**Short answer:** The patch as written is unlikely to be accepted unchanged. The DT maintainers have already turned down two touchscreen properties worded this way, calling them "describes Linux behaviour, not hardware". The easiest route is to drop the binding patch and detect the quirk inside the driver. This also means pem120 only has to get it past the input maintainer, not the DT maintainers as well.

## Rules that apply (from `/home/user/linux`)
- **Hardware, not software.** A binding must describe the hardware and must not mention Linux or a driver (writing-bindings.rst:21-22). Adding a property to stand in for a specific compatible, or one that the compatible already implies, is not allowed (:85-86). New device bugs should normally get a new compatible (:68). Device-specific properties need a vendor prefix (:78); `atmel,` is fine.
- **Old behaviour is kept.** When the property is missing, the driver must behave as before (ABI.rst:29-32). The patch does this.
- **Process.** The binding goes in its own patch, before the driver patch, with a `dt-bindings:` subject. It must pass `dt_binding_check` and be sent to the devicetree list (submitting-patches.rst:13-16, 35-39, 50-58). The patch does all of this. Patches marked RFC are usually ignored by the DT maintainers (maintainer-devicetree.rst:46).
- **No rule requires an in-tree DTS user.** The rule in submitting-patches.rst:60-66 only covers the other direction (compatibles used in a DTS must be documented).
- **Precedent without a DTS user.** Both the atmel `poweroff-sleep` v2 series and the goodix `no-reset-during-suspend` series were binding plus driver only, with no DTS patch. Both still got DT review tags.
- **Checks I ran.**
  - `dt-doc-validate` (dtschema 2026.9) passes on the patched YAML (meta-schema only; I did not rebuild the examples).
  - checkpatch reports `ERROR: Missing Signed-off-by` and a warning about `Co-Authored-By` capitalisation. The kernel's AI rules say pem120 must add his own Signed-off-by and an `Assisted-by:` line (coding-assistants.rst).

## How reviewers reacted to similar properties
- **`atmel,poweroff-in-suspend` v1 (same binding file).**
  - Krzysztof Kozlowski: "You described the desired Linux feature or behavior, not the actual hardware" (`<b9868bd4-6f14-4628-88ea-56d06027739e@linaro.org>`).
  - Conor Dooley asked the author to answer "why can't we … on all systems with this device" (`<20231209-sizzle-monthly-6e4f3c966b0f@spud>`).
  - Dmitry Torokhov (input maintainer) argued it is "also a property of a board" (`<ZXOoy8mFdhUQsZAu@google.com>`).
  - v2 was reworded and got Krzysztof's Reviewed-by (`<91dd66ac-…@linaro.org>`). The series still stalled at v6 in driver review (`<Zqg6tNbCn3W79Li_@google.com>`) and is not in v7.3-rc4.
- **goodix `hold-in-reset-in-suspend` (a hobbyist submitter).** Krzysztof: "You describe system policy… DT is not for policies" (`<4cdefb22-…@linaro.org>`), followed by a NAK (`<1ae4767f-…@linaro.org>`).
- **goodix `no-reset-during-suspend` (Chromebook).** This one was presented as a hardware flaw that leaks power, and Rob Herring acked it (commit 359ed24a0dd3). How the property is worded clearly decides the outcome.
- **`linux,keycodes` (272a26186a).** Acked by Krzysztof. Its commit message says the property "enables those keys".
- **Maintainer response time.** Hendrik Noack's atmel_mxt_ts series (Mar–May 2026, v3 `<20260528074317.9604-2-hendrik-noack@gmx.de>`) got no human maintainer reply, only an automated review bot. Dmitry has also said the automatic config download "was a mistake and I would like to get rid of it" (`<ZnOCDaetFnsg09if@google.com>`).

## Problems with the patch as written
1. The description ("Enable … whenever the device is started") tells the driver what to do. That is the exact wording reviewers rejected above.
2. "Some controllers" is vague. The commit message never names the device and never says why this can't be the default or a compatible.
3. The chip's config CRC (0x55571E) can be read at runtime. If the commit message says so, reviewers will likely reply that the quirk can be detected, so no DT property is needed.

## Alternatives

| Option | DT acceptability | Risk / cost |
|---|---|---|
| A. As written | Low | None to other boards |
| B. Same property, reworded as a hardware condition (stored config leaves T97 off; the chip reports no touches until it is on) | Plausible, following the poweroff-sleep v2 and goodix pattern | Reviewers may still say "detect it in the driver" |
| **C. Driver quirk keyed on chip IDs (a4/15/2.1.AA) plus config CRC 0x55571E** | No binding needed | Only units running Xiaomi's config are affected, and the vendor driver already does this write on them. IDs alone are not enough: a genuine mXT336T has the same IDs and also has T97 (datasheet ds336t.txt:1142-1146). The driver only asks the chip for its CRC when uploading a config (atmel_mxt_ts.c:1569), so one extra request is needed. **That this request returns 0x55571E before `mxt_start()` has not been checked on the device.** |
| D. Enable T97 whenever `linux,keycodes` is set | No new ABI; matches the wording of 272a26186a | Changes behaviour on 4 in-tree boards (gt510, l9100, matisse, mocha) that nobody can test. The write can't be read-modify-write on this chip. Touch would silently depend on the keys being listed. |
| E. Chip-specific or board-specific compatible | Poor | Wrong for real mXT336T parts; the maker of the clone chip is unknown; the binding only has `atmel,maxtouch` (yaml:22-23) |
| F. Board-level quirk (`of_machine_is_compatible("xiaomi,ferrari")`) | No touch binding needed | Needs the ferrari DTS and its `qcom.yaml` entry upstream first. No input driver does this today, though the driver already has a similar Chromebook quirk table (:3236). |
| G. Always enable T97 | n/a | Changes defaults for every board with T97. On mXT336T the T97 key-array pins double as GPIO/NOISE_IN/SYNC (ds336t.txt:288-291, 1000). Reject. |
| H. `maxtouch.cfg` | n/a | Re-uploads and writes to the chip's flash (NVM) on every boot, and Dmitry wants to remove this mechanism. Reject. |

**Recommendation:** Go with **C** and drop 4ffb88519f. There is then no DT ABI to argue about, no need for an upstream ferrari DTS, and nothing changes on other boards. The cover letter should state that the CRC read was checked on the device. Keep **B** ready in case Dmitry asks for an explicit opt-in in DT. If the ferrari DTS goes upstream later (through the Qualcomm tree), that makes either route stronger.

## Likelihood (my estimates, not measured)
- **Binding merged as written:** about 10–15%. More than 70% chance the first round gets a "describes driver behaviour" reply.
- **Reworded version (B):** about 50% chance of a DT ack within 1–2 rounds. Whether it actually merges depends mostly on Dmitry's response time, maybe 30–40% within 2–3 kernel cycles.
- **Route C:** avoids the DT maintainers entirely. Its chances depend only on Dmitry.

## What I could not verify
- Lore search is blocked for this machine (HTTP 403 from lore's bot protection). I found threads through the patchwork API and fetched them by message-id, so there may be other "enable-*" precedents I missed.
- I don't know the other bits in the T97 control byte.
- I don't know whether original, non-replacement ferrari panels report the same CRC.
- The DTS copy at `.../scratchpad/dtsw/.../msm8939-xiaomi-ferrari.dts` is an older version. It has `linux,keycodes` and no `atmel,enable-t97`, which does not match the draft commit message in `series2/5-dts.txt`.

Files are in `/tmp/claude-0/-home-user-linux/33841751-cb18-58d1-b467-70ec7733370c/scratchpad/upstream-review/dt-binding/`:
- `*.mbox.gz`: the downloaded review threads
- `ext.py`: script that extracts replies from them
- `binding.patch`: the binding patch as reviewed