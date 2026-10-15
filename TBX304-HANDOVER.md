# TB-X304 kernel handover

## Status and scope

This is a frozen work-in-progress snapshot, not an upstream-ready series.
Continue the kernel work independently of PocketBoot.
The original goal was initial mainline support for the Lenovo Tab 4 10
TB-X304F and TB-X304L: USB, bootloader-initialized display, and physical keys.
Touch and storage were added during bring-up.

The disputed APCS clock patch and its device-tree conversion remain in this
snapshot. Their removal was discussed but was not implemented before the
request to freeze the work. Their necessity remains unproven.
Do not interpret their presence as an endorsement or a successful CPU-clock
runtime test.

The published branch is `samcday/linux:wip/tbx304`.
It contains one snapshot commit above
`16bba1965f25556ae0255a13c4b20996711eddf6`.
That base contains Torvalds v7.3-rc6,
`a90ee4305c4a5df72c11b31dacfdc76e00fcf78a`, plus two existing GitHub workflow
commits. Those workflows are unchanged.
Do not advance the base as part of recovering this work.

Earlier source reference:
`ae9e8d65b8215f4af6b999c5f865a4b4f7b960cb` on `wip/tbx304-touch-ci`.
This snapshot adds the previously uncommitted 30-line storage description
to that source, plus this report. It does not add display diagnostics,
reboot experiments, PRNG changes, or a driver-core fix.

## Source inventory

The functional diff against the preserved repository base is five files,
315 insertions and 9 deletions:

1. [Qualcomm board bindings](Documentation/devicetree/bindings/arm/qcom.yaml#L265):
   Add `lenovo,tbx304x` under MSM8917.
2. [Qualcomm DTB build list](arch/arm64/boot/dts/qcom/Makefile#L110):
   Add `msm8917-lenovo-tbx304x.dtb`.
3. [Board description](arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dts#L1):
   Add the shared F/L device tree, including touch and storage.
4. [MSM8917 description](arch/arm64/boot/dts/qcom/msm8917.dtsi#L1579):
   Move CPU clocks into an APCS child, update four CPU references, and
   change the RPM transport from `qcom,ipc` to `mboxes`.
5. [APCS clock driver](drivers/clk/qcom/apcs-msm8916.c#L85):
   Change `devm_clk_get(parent, NULL)` to `devm_clk_get(dev, NULL)`.

The last two files form the disputed workaround. Do not drop only the C
change while retaining a device tree that expects its child-node lookup.

Source SHA256 values at freeze:

```text
36156f57a3874446083e3074a03a762d6fae721c07436c57b4dc90b01267e902  arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dts
30cf0425bba59363e2c3d781f6f8d46c5add4b9538aad50fe2df646c07ab8377  arch/arm64/boot/dts/qcom/msm8917.dtsi
3fc9c68adb980bd1b71433bd48dab416719300deaa0598d1c8c459785cde08e4  drivers/clk/qcom/apcs-msm8916.c
```

## Hardware and retained wiring

Sam's TB-X304F is the Wi-Fi variant, APQ8017, SoC ID 307.
Ishant's TB-X304L is the LTE variant, MSM8917, SoC ID 303.
Both use board ID `<0x1000b 0>`, PM8937, and an 800-by-1280 NT35521S-family
panel. The L panel was identified as a BOE variant.
The shared root compatible is `lenovo,tbx304x`, followed by `qcom,msm8917`.

The [framebuffer node](arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dts#L20-L47)
describes RGB888 at `0x90001000`, 800 by 1280 pixels, with a 2400-byte stride.
It is disabled in the source. A bootloader must provide the working
framebuffer handoff. This is not a native panel driver.

The [USB description](arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dts#L283-L295)
uses ChipIdea peripheral mode and `usb-role-switch`, with the L2/L7/L13 PHY
supplies. Host mode and charging are outside the established baseline.
The successful old PocketBoot images also explicitly requested USB device
role from userspace. Those tests do not prove automatic role selection.

The [Goodix GT911](arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dts#L84-L102)
is on BLSP1 I2C3 at address `0x5d`.
Reset is GPIO64, interrupt is GPIO65, AVDD28 uses L10, and VDDIO uses L6.
Both axes are inverted. `FW_LOADER` was necessary in the minimal kernel.
The missing optional `goodix_9111_cfg.bin` message did not prevent input
registration or the confirmed F-unit touch test.

Volume Up uses GPIO91. Volume Down uses PM8937 RESIN.
Keep complete physical-key validation separate from input-device
registration or synthetic evdev tests.

The [storage nodes](arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dts#L229-L245)
use L8/L5 for eMMC and L11/L12 for microSD.
The aliases assign eMMC to `mmc0` and microSD to `mmc1`.
Card detect is GPIO67, active high, with a pull-up.
Do not reverse that polarity based only on the pull-up.
The L-unit test detected an inserted card with this description.

## What the hardware tests establish

Earlier images booted through lk2nd on both units with USB and visible UI.
Sam confirmed touch input on the F unit.
The main-thread record contains L-unit touch registration, but does not
establish a complete L-unit touch or corner-coverage test.

Storage was tested with the source represented by this snapshot:

| Unit | Result | Limits |
| --- | --- | --- |
| F | HS400 `QE13MB`, 30,535,680 sectors, all 48 partitions, both boot areas and RPMB detected | No write, suspend, or eMMC OS-boot test |
| F | Read 17,408 bytes containing the primary GPT. Header and entry-array CRCs passed | Linux write counters remained zero |
| F | SD host and card-detect GPIO registered | No card detected. Physical insertion was not confirmed |
| L | HS400 `HAG4a2`, 30,777,344 sectors, all 48 partitions, boot areas and RPMB detected | No eMMC write test |
| L | High-speed 14.8 GiB `SU16G` SDHC card and two partitions detected. Boot files read | Hotplug and suspend/resume untested |

The F primary-GPT read matched the earlier capture:
`d71622f41619be3ac060171252de68dee0a818c322fae22ebea6ce809f8b86f2`.

The F storage test used a direct-stock framebuffer overlay. Its boot-image
SHA256 was
`323f8cb8e3652081eaf06083d069c69d38405e2fea6d7ebb527154401df82472`.
The L storage test used lk2nd and image
`d0f4f2bea63fe814b5420dca6cc87cd1dc3984af43031b825d060fe34cdac93a`.
Both images included the old userspace USB-role experiment.
Neither image is a validation of the new PocketBoot snapshot that excludes it.

Ishant later reported a successful Mobian boot from SD.
That used a different temporary PocketBoot image with additional bootflow
changes. It supports SD-read evidence, not eMMC OS-boot or CPU-clock coverage.

## APCS: unresolved question, not a proven prerequisite

The minimal working configuration contained `CONFIG_MODULES=y`, despite
building all selected platform drivers into the kernel.
It omitted both `CONFIG_QCOM_A53PLL` and `CONFIG_QCOM_CLK_APCS_MSM8916`.
The latter controls whether the patched clock-driver object is built.
Therefore the successful minimal boots did not execute the one-line C patch.

The statement "no loadable modules means no deferred probing" is incorrect.
The [late-init path](drivers/base/dd.c#L340-L371) retries deferred probes
with either setting. Disabling modules changes
[dependency failure policy](drivers/base/dd.c#L285-L297) after initialization.
The [default timeout](drivers/base/Kconfig#L76-L83) is zero without modules
and ten seconds with modules. The old test command line overrode it to five.

There is another concrete dependency in the unmodified MSM8917 layout.
The [legacy RPM transport](drivers/rpmsg/qcom_smd.c#L1379-L1393) obtains the
APCS syscon register map.
[Syscon registration](drivers/mfd/syscon.c#L125-L135) requests its first clock,
which is `a53pll` in the original device tree.
A missing PLL provider can return `-EPROBE_DEFER`.
A full distribution kernel can include providers omitted by the minimal
configuration. That difference must not be mistaken for proof of a
device-link cycle or proof that the workaround is necessary.

The intended next comparison was the original upstream clock layout with
its required providers enabled. That comparison was not performed.
It requires restoring both changed files, including `qcom,ipc`, the four CPU
clock references, and the APCS parent clock properties.
Enable `CONFIG_QCOM_A53PLL=y` and `CONFIG_QCOM_CLK_APCS_MSM8916=y` in the
comparison kernel. Keep the base, boot path, and other configuration fixed.
Record the effective configuration, driver binding, clock registration,
deferred devices, and log. Do not claim CPU-frequency coverage from boot alone.

The C patch comes from Stephan Gerhold:

- [Original patch 3/4](https://lore.kernel.org/r/20250506-qcom-apcs-mailbox-cc-v1-3-b54dddb150a5@linaro.org)
- [Original series](https://lore.kernel.org/r/20250506-qcom-apcs-mailbox-cc-v1-0-b54dddb150a5@linaro.org)
- [Stephen Boyd's objection](https://lore.kernel.org/r/175053907628.4372.13105365536734444855@lazor)
- [Bjorn Andersson's objection](https://lore.kernel.org/r/qawoswiuojk7no32jf2vejh4wcocv74fn37kn5xjqbbnwshmkv@r6yitwwx64xb)

The clock-child binding and mailbox child-node support already exist in the
selected base. The clock lookup change does not.
Earlier research found no later revision of that clock patch.
`post-init-providers` was discussed, but it suppresses dependency links.
It was not established as an equivalent solution for probe or suspend order.
An earlier MSM8939 mailbox-only conversion was reverted as
`d92e9ea2f0f918d7b01cbacb838288bffccc8954`.
Do not repeat a mailbox-only partial conversion.

## Display and restart problems remain separate

The source framebuffer expects a bootloader handoff, normally lk2nd.
Sam also tested direct stock boot with an experimental overlay that enabled
the framebuffer and reserved `[0x90001000, 0x902ef000)` with `no-map`.
That overlay and its repacker are not kernel changes in this snapshot.

The direct-stock path can retain a solid blue screen after Shutdown with
USB power attached. USB power can trigger an immediate new boot.
The failure also survived warm restarts.
Charger-mode entry is not a sufficient explanation.

The separate display investigation captured a physically blue state and a
working state with identical live device trees and framebuffer bytes.
RAM contained the normal menu. The active RGB0 address, dimensions and stride
matched, and frame counters advanced.
The shared framebuffer SHA256 was
`b187e5a8c221f896531611993f7d6e914df5f00f4c02c98696c7b3435472732c`.

Sampled DSI error flags matched in both states, including FIFO status
`0xcccc1000`, ACK status `0x01000040`, and interrupt status `0x01010000`.
The sampled MDP underrun bit was set in the healthy capture and clear in the
blue capture. These latched flags did not distinguish the failure.
Recovery with reboot plus quick USB disconnection was observed, but it was
not a verified full cold power-off.

A later interface MISR, a hardware pixel checksum, changed with drawn content.
Normal-menu samples were `0x2ce0ee41`. Power-menu samples were `0x49af4d16`.
The missing experiment was a physically confirmed blue-versus-good MISR pair.
Unrequested USB disappearances also interrupted testing.
The display issue was not diagnosed or fixed.

Sam moved that investigation to MimoCode in another harness.
Obtain its handover and device ownership before further tablet operations.
The reported portable bundle was about 6.3 MiB, SHA256:
`7c8353057b2e766e42001ed80d3af16fd69858d53f503e36889879ac4d9a9d98`.
It includes the control image, captures, and guarded tools.
The source handover was named `tools/tbx304-bluescreen-handover.md`.
Neither those tools nor the bundle are included in this snapshot.

The last verified installed F control-image payload was 4,947,968 bytes,
SHA256 `2652e47c43a4bb16e346cee57c91ae9fe906c724778d83f0a242d49ace9a09d7`.
This is historical state, not permission to assume what is installed now.

The stock bootloader also did not honor the earlier PocketBoot
`reboot bootloader` request. Downstream code points to an IMEM restart cookie,
while the attempted path used a PMIC reason.
A separate reboot thread exists. No IMEM fix was absorbed into this snapshot.
Do not promise software recovery to stock fastboot.

A ramoops recipe was prepared but never booted.
It reserves `[0xbff80000, 0xc0000000)`, with four 128 KiB dump slots,
`max-reason = <4>`, and `pstore.kmsg_bytes=131072`.
It has no continuous-console allocation.
The installed control did not reserve this memory for ramoops.
Range safety, actual logging, and retention through the bootloader remain
unproven. A hard reset that bypasses `kmsg_dump()` can leave no new dump.

## Review, provenance, and consolidation

The old private core series ended at `5c5725b80f4e` on `prep/tbx304-core`.
It separated Stephan's clock patch, the MSM8917 conversion, the binding,
and a no-touch board description.
Patch 2 became `d6af6819841d0` after a message-only amendment adding:

```text
Fixes: 7f18b1ea7987 ("arm64: dts: qcom: Add initial support for MSM8917")
```

The earlier series review found no confirmed functional regression.
That was not CPU-clock runtime validation or maintainer approval.
The later focused review highlighted the unmerged prerequisite and the
unresolved clock-child objections.
Do not reuse that review verdict as approval of this larger touch/storage WIP.
The private commit objects are not guaranteed to exist in another clone.

Ishant Kuchibhatla <18i897.iiibb@gmail.com> is the intended primary upstream
author. Sam Day <me@samcday.com> is the intended co-author.
The board also retains substantial ahi6 work, including framebuffer,
regulator, USB, and oscillator wiring.
The source SPDX identifier remains `GPL-2.0`.
The reported permission to remove a copyright line is not a new sign-off.
Humans must settle attribution and supply certification for the eventual
series. Do not invent `Signed-off-by` trailers.

Eight related DT-review, minimal-kernel, touchscreen, landing, cleanup,
dependency-cycle, and prerequisite-research worktrees were merged during
consolidation. Their unique additions were principally review/provenance
records. The tested board source was already present.
The old kernel-only split also removed unrelated workflow files.
Those files were restored exactly.
Generated review files and build output are not part of this snapshot commit.
Do not merge all historical worktrees again.

No TB-X304 series was sent with `b4 send`.
No upstream landing occurred.
The new branch is a single checkpoint, not the final patch decomposition.

## Validation at freeze

These standalone kernel commands passed without using a PocketBoot build:

```sh
make O=.tmp-tbx304-freeze ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make O=.tmp-tbx304-freeze ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  W=1 CHECK_DTBS=1 DT_SCHEMA_FILES= -j2 \
  qcom/msm8917-lenovo-tbx304x.dtb \
  drivers/clk/qcom/apcs-msm8916.o drivers/clk/qcom/a53-pll.o
dt-validate -m \
  -s .tmp-tbx304-freeze/Documentation/devicetree/bindings/processed-schema.json \
  .tmp-tbx304-freeze/arch/arm64/boot/dts/qcom/msm8917-lenovo-tbx304x.dtb
git diff --check
```

Direct schema validation returned no diagnostics.
DTC emitted eight warnings in the inherited MSM8917/PM8937 descriptions.
Earlier before/after storage compilation proved those warnings unchanged.
This freeze compiled the two clock objects, not a full kernel image.
No new hardware, CPU-frequency, suspend/resume, or legacy-device tests ran.

## Next kernel-only attempt

1. Read this report and inspect the five-file functional diff.
2. Keep the fixed upstream base and a working recovery route.
3. Settle the APCS comparison before claiming a prerequisite is necessary.
4. Use a documented kernel configuration independent of PocketBoot changes.
5. Repeat board tests on both F and L, with image and configuration hashes.
6. Decide the upstream scope explicitly: core, touch, and storage are separable.
7. Resolve human attribution, sign-offs, and outstanding review objections.
8. Prepare a new small series and run non-sending `b4` checks before sending.

Do not add charging, PRNG, native display, or userspace work to that series
without a separate scope decision. Do not flash either tablet without the
owner's approval for the exact image.
