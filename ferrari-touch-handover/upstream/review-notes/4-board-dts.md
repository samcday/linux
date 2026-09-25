## Board/DTS path: findings

**Verdict:** pem120 can realistically get the driver and binding patches onto LKML. The board DTS is a separate, larger job, and parts of the downstream file can't go upstream as they are. The practical order is: send the driver and binding patches first. Put a trimmed DTS into the msm8916-mainline fork now, then send it upstream once the binding is in linux-next.

### 1. Where a ferrari DTS exists today
- **Upstream 7.3-rc4:** there is no `msm8939-xiaomi-ferrari.dts`, and `xiaomi,ferrari` is missing from `Documentation/devicetree/bindings/arm/qcom.yaml`. The msm8939 enum there (lines 280-290) lists only asus,z00t, huawei,kiwi, longcheer,l9100, samsung,a7, sony,kanuti-tulip, square,apq8039-t2 and wingtech.
- **msm8916-mainline fork:** no ferrari DTS on `wip/msm8916/7.0` (the default branch), `wip/msm8916/7.3-rc2`, `wip/msm8916/6.19`, `msm8916/6.12` or `msm8916/6.6`. I checked with partial clones and `git ls-tree`. There is no ferrari panel driver in `drivers/gpu/drm/panel/msm8916-generated/` either.
- **lore:** `dfn:msm8939-xiaomi-ferrari.dts` returns 0 hits across all lists, so nothing has ever been posted. Plain "ferrari" searches only find "Arnaud Ferraris".
- **Earlier port:** in 2020 SUNGOLDSV (suraajvashisht@gmail.com) ported ferrari on github.com/SUNGOLDSV/msm8916-mainline-linux, branch `v5.9-rc1-ferrari`, commit e3feeb4. He also fell back to the vendor atmel driver because upstream `atmel_mxt_ts` did not work (https://lore.kernel.org/all/20200909072856.GB4400@dell/). That was presumably a stock unit, which hints that stock units hit the same problem. I have not verified that.
- **postmarketOS:** `device/archived/device-xiaomi-ferrari` exists, but it uses the 3.10 downstream kernel and was archived as unmaintained on 2026-06-22.
- **Who wrote the current downstream DTS:** unknown. The file has no header, the DS tree isn't public, and the local `ds-series` patches say `From: pem120`. The node layout differs from SUNGOLD's file, but they share vendor-derived content, so the submitter should decide whether to credit him.

### 2. How recent msm8939 boards got in (git.kernel.org log)
| Board | Author | Initial commit |
|---|---|---|
| asus-z00t | Erikas Bitovtas | 42621cbb3afd, 2025-10 |
| wingtech-wt82918 | Adam Słaboń | 9a2ec63ae683, 2024-07 |
| huawei-kiwi | Lukas Walter | cff9a76f306b, 2023-12 |
| longcheer-l9100 | André Apitzsch | 27da4fd325c3, 2023-09 |

All four were written by hobbyists. z00t shows the typical process:
- **Shape:** two patches, one adding the board to `qcom.yaml` and one adding the DTS plus Makefile line. The first version supported UART, USB, eMMC/SD, keys, touch, sensors, audio and modem.
- **Speed:** v1 was posted 2025-09-30 and Bjorn applied v2 on 2025-10-27.
- **Review:** only nits. Konrad asked to sort pin nodes and add blank lines; Krzysztof sent a form letter about a dropped Reviewed-by tag; Rob's bot ran dtbs_check. Links: https://lore.kernel.org/linux-arm-msm/7abe83dc-5469-48e1-8964-ce3377d82a4d@oss.qualcomm.com/ and https://lore.kernel.org/linux-arm-msm/176160465208.73268.15556003473353414195.b4-ty@kernel.org/
- **Rules:** `Documentation/process/maintainer-soc.rst:169-175` requires no new `dtbs_check` warnings and no `dt-check-style` warnings in relaxed mode.
- **Touch node precedent:** l9100 (lines 226-242) uses the same pins and `atmel,maxtouch`: GPIO13 level-low, GPIO12 reset, and a GPIO78 2.85 V `reg_ts_vdd`.

### 3. Review of the downstream DTS
I ran `dtbs_check W=1` on the DTS copy, changed to the final form (`atmel,enable-t97`, no keycodes), in a scratch worktree on 536df0e49b. Log: `.../upstream-review/dts/check-final.log`. These problems must be fixed before upstreaming:
1. **Display:** `xiaomi,ferrari-panel` matches no schema, and there is no driver for it upstream or in the fork. The panel is also the aftermarket one. Drop `mdss`, `mdss_dsi0*`, the panel and `mdss_default`/`mdss_sleep`; those pin nodes also fail the pinctrl schema.
2. **Backlight:** `ti,lm3533` matches no schema in 7.3. Svyatoslav Ryhel's OF conversion was only applied to Lee Jones's tree for 7.4 (https://lore.kernel.org/all/178904903999.1172658.13303214372114400299.b4-ty@b4/). The DTS's `led-max-microamp = <4744>` is also below that binding's minimum of 5000.
3. **USB:** `dr_mode` in `&usb_hs_phy` fails `qcom,usb-hs-phy.yaml`.
4. **Leftovers:** `// FIXME` on model and compatible, a disabled simple-framebuffer at `@83000000` with `reg` 0x83200000 that `stdout-path` points to, commented-out ramoops, and cont_splash memory. None of the upstream msm8939 boards have a framebuffer.
5. **Touch power:** the model feeds a 1.8 V GPIO9 "ts-ldo" into the 2.85 V GPIO78 rail. That can't work electrically, and the draft commit message says the voltages were never measured. The vendor DT only names the GPIOs.
6. **Missing basics:** volume keys (gpio-keys and `pm8916_resin`). SUNGOLD's 2020 DTS had gpio-keys.

With items 1-4 removed, `ferrari-minimal.dts` (146 lines, in the same folder) passes `dtbs_check W=1`. The only warnings left come from the msm8939 dtsi and show up on z00t too, and relaxed `dt-check-style` is clean (`check-min.log`).

**Effort:** a basic DTS (UART, USB, eMMC, Wi-Fi, keys, touch) is about 1-3 evenings of work plus one or two review rounds, probably 3-6 weeks elapsed for a first-time submitter. Display and backlight are separate, bigger projects.

### 4. Should a board DTS for all ferrari units set T97-enable?
- **Claim verified:** the vendor driver's `mxt_resume()` calls `mxt_set_ptc_enabled(data, true)` unconditionally. That writes CTRL=3 to T97 instances 0-2 on every resume, whatever the panel ID or config, and suspend clears them (`bundle/src/atmel_mxt_ts_336t.pristine.c:4794-4810, 4829, 4876`). So enabling T97 matches what stock units do.
- **Harmless without keycodes:** upstream ignores T97 key bits when `linux,keycodes` is absent (`atmel_mxt_ts.c:915`, `t15_num_keys`=0).
- **Not shown to be needed on stock units.** The vendor driver also uploads a config for each panel (biel/tpk/wintek). Only one unit, with the aftermarket panel, was tested. The commit message has to say this.
- **Evidence conflict:** REPORT.md:57 says the keys are dead in hardware. But the MIUI log from the same serial and the same config (0x55571E) shows T97 press and release (`E08-REPORT-part1.md:32-34`, `E06 REPORT.md:166-172`). Either leave the keys out or retest before calling them dead.

### 5. Sequencing
1. **Driver and binding first**, to linux-input and the DT list. Upstream DT rules make any use of `atmel,enable-t97` in a DTS wait until the binding is merged, and the property name could still change in review.
2. **DTS to the fork first.** The fork's CONTRIBUTING.md says changes to shared drivers should go upstream before the fork takes them. It welcomes device DTS PRs for early review, and it feeds pmOS's `linux-postmarketos-qcom-msm8916`. It also requires Signed-off-by.
3. **Upstream DTS afterwards**, as `qcom.yaml` + DTS without display or backlight, once the binding is in linux-next. Alternatively, post it without the property and add it in a later patch.

### Not verified
- **i2c-qup hack still in the tested kernel:** the 7.0 test kernel still carries a non-upstream QUP change that forces DMA and custom dividers. A kernel with that change reverted never brought up USB (REPORT.md:38-42, `ds-series/0005`). Touch has not been shown to work on an unmodified upstream QUP.
- The final DTS in the DS tree; I tested my own reconstruction of it.
- Suspend/resume proper.

Files are in /tmp/claude-0/-home-user-linux/33841751-cb18-58d1-b467-70ec7733370c/scratchpad/upstream-review/dts/:
- ferrari-final-variant.dts
- ferrari-minimal.dts
- sungold-ferrari.dts
- check-final.log
- check-min.log