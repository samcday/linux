# Sony Xperia 1 VI: mainline-DT bring-up

This is a **new, source-derived, hardware-untested** `pdx245` board description.
It is not an existing proven port, an Android boot image, or a flashing guide.
The CI artifact contains a DTB and validation evidence only.

The target is `qcom/sm8650-sony-xperia-asahi-pdx245.dtb`, with mainline compatible
`sony,pdx245`, on the SM8650 SoC support already in this kernel. It includes
`sm8650.dtsi` directly rather than importing an entire Qualcomm QRD board.

## Evidence and interpretation

The hardware reference is
[LineageOS/android_kernel_sony_sm8650-devicetrees at fd3157426ad1ddf84e3b5cf2a8cc334c3692f2ba](https://github.com/LineageOS/android_kernel_sony_sm8650-devicetrees/tree/fd3157426ad1ddf84e3b5cf2a8cc334c3692f2ba).
The entry point is
[`sony/pineapple-asahi-pdx245_generic-overlay.dts`](https://github.com/LineageOS/android_kernel_sony_sm8650-devicetrees/blob/fd3157426ad1ddf84e3b5cf2a8cc334c3692f2ba/sony/pineapple-asahi-pdx245_generic-overlay.dts).
Its effective description combines the Qualcomm Pineapple base/QRD includes
with Sony's `pdx245_generic`, `pdx245_common`, and `asahi-common` overrides.
These are Android/vendor bindings: they are references for hardware facts,
not DTS fragments that can be included in a mainline build.

| Mainline description | Source and boundary |
| --- | --- |
| Board identity | The Sony entry point names PDX-245. Its stock compatible is `somc,pdx245-generic`; the new mainline binding follows the existing `sony,pdx234` naming convention. |
| Debug UART | `qcom/pineapple.dtsi` selects `0x89c000` at 115200 baud. Sony's `pineapple-asahi-common.dtsi` labels GPIO30/31 as debug TX/RX. These correspond to mainline UART15, not the older Xperia's UART7. Physical access and firmware enablement remain untested. |
| PMICs | The reference puts PMK8550 at SPMI SID 0, PM8550 at 1, and PM8550B at 7. Only the PMIC descriptions required for this first stage are included. |
| Buttons | Sony overrides PMK8550 PON resin to volume-up. PM8550 GPIO6 is volume-down, not the QRD's volume-up. |
| Oscillators | The reference declares XO 76.8 MHz and sleep 32 kHz. The latter differs from the QRD's 32764 Hz; it is a source-derived value, not a measurement. |
| Fixed memory | The SM8650 reservations cover the reference's fixed memory map after adding the lost-register area at `0x9b09c000` (16 KiB) and moving hardware-fence memory to `0xd4e23000` (size `0x2dd000`). The QRD/default `0xe6440000` hardware-fence reservation is removed. |
| Modem storage memory | The Android reference does not identify a fixed rmtfs allocation. The SoC's 4 MiB reservation at `0xd7c00000` is conservatively retained, without the compatible/client/VM properties that would assign it to a modem. This allocation needs firmware evidence before modem enablement. |

### USB and storage boundaries

- **USB2 diagnostic candidate:** the mainline DWC3 controller is fixed to
  peripheral/high-speed mode, referencing only the eUSB2 PHY and PM8550B
  repeater, with UTMI selected as its PIPE clock source while SSPHY is disabled.
  The vendor's `pineapple-usb.dtsi` connects the PHY to VE L1/L3;
  these are RPMh group **i**, distinct from its numeric SPMI SID 8. The LDO
  voltage ranges follow `pineapple-regulators.dtsi`.
- **Repeater assumptions are explicit:** the Android DT names the repeater but
  does not name its two external supply rails. L15B/L5B is the mainline SM8650
  QRD/HDK crosswalk, not independently established Sony wiring. It needs
  confirmation from hardware/stock firmware. The inherited vendor tuning for
  amplitude, disconnect threshold, pre-emphasis, and squelch is translated to
  supported mainline properties. Vendor register `0x59` slew tuning has no
  corresponding mainline property and is not copied.
- **No Type-C claim:** Sony removes the QRD GPIO29/redriver setup. The vendor
  delegates attach/role handling to PMIC-GLINK/UCSI; whether its firmware leaves
  CC termination and VBUS usable for this static peripheral path is unknown.
  No charging, host role, role switching, SuperSpeed or DisplayPort is enabled.
- **UFS is described but disabled:** `pineapple-qrd.dtsi:50-97`, inherited by the
  Sony entry point, identifies GPIO210 reset, PM8550 L17 VCC, VS-C L1 VCCQ,
  VS-D L1 PHY, and VE L3 PLL supplies. It also explicitly votes VS-C L3
  (`VDD_PX10`) for `ufs_reset_n`. That vendor-only supply has no equivalent
  in the current mainline UFS binding. Do not silently discard that requirement
  or turn every rail always-on to make the DTS appear complete. Host and PHY
  remain disabled until reset-pad power and the Qref/parent-supply handling are
  resolved. A first diagnostic boot must use RAM, not depend on UFS.

RPMh regulator groups describe command-DB resources and do not require every
PMIC's SPMI child devices to be enabled. Apart from the source's explicit
VCCQ-parent relationship, other regulator upstream supply nets are not inferred
from the HDK; their existing firmware votes remain an additional bring-up
dependency to check.

An independent comparison covered all **36 fixed reservations** in the
downstream Pineapple base with the compiled DTB and found no overlaps in the
resulting reservations. This does not establish the final RAM map for every
Sony firmware release: bootloader fixups and dynamically removed hypervisor
regions still matter. Do not invent RAM sizes, a framebuffer address, or a
persistent ramoops address from a different Xperia.

Display, GPU, cameras, audio, modem, WLAN and DSP firmware startup are not
enabled. No firmware blobs are bundled. A black display is expected for this
stage; it must not be used as the sole indication of whether Linux started.

## Build and validate

Required tools include an AArch64 compiler, make, flex/bison, dtc, and
`dtschema==2026.9`. The fork-local workflow `.github/workflows/pdx245-dt.yml`
installs these and publishes `dtb-sm8650-sony-xperia-asahi-pdx245`.

```sh
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_OUTPUT="$PWD/.pdx245-build/out"
dt-doc-validate Documentation/devicetree/bindings/arm/qcom.yaml
make defconfig
make -j8 W=1 CHECK_DTBS=y qcom/sm8650-sony-xperia-asahi-pdx245.dtb

dtb="$KBUILD_OUTPUT/arch/arm64/boot/dts/qcom/sm8650-sony-xperia-asahi-pdx245.dtb"
dt-validate \
  -s "$KBUILD_OUTPUT/Documentation/devicetree/bindings/processed-schema.json" \
  --json-output .pdx245-build/schema-result.json "$dtb"
python3 .github/scripts/check-pdx245-dtb.py \
  --schema-result .pdx245-build/schema-result.json "$dtb"
```

Require an empty schema-diagnostics array (`[]`), not merely a successful
`make` exit. The workflow fails on nonempty diagnostics and checks the actual
compiled board identity, console, USB PHY/supply references, deliberately
disabled UFS, reserved-memory coverage points and overlap invariants.
Negative-control DTBs carrying the QRD compatible, enabling UFS, removing
the USB PHY supply, or omitting USB2's UTMI clock selection must each be rejected.
The checker also rejects a nonempty schema-diagnostics fixture.

`W=1` currently reports two inherited `avoid_unnecessary_addr_size` warnings on
the disabled SM8650 DSI controllers. They are not evidence of a working panel;
do not enable a display or alter the shared SoC description to hide them.

Artifacts include the source commit, schema version, schema diagnostics,
this note, and a DTB checksum. They do not include a kernel or `boot.img`.
Passing these checks establishes structural consistency, not hardware support.

## Gates before a pocketboot image or hardware trial

1. Confirm the exact Xperia variant, firmware, existing unlock eligibility,
   and a non-destructive custom-boot route. US/JP unlock restrictions and carrier
   restrictions cannot be inferred from the model name alone.
2. Obtain the stock image/DTB/DTBO metadata and, if already accessible, the live
   Android DT and RAM reservations. Compare them with the source-derived map.
3. Establish how this bootloader selects and validates a DTB. The stock overlay
   advertises SoC IDs 557/577 and board IDs `0x1000b`/`0xb`; these are evidence
   for future boot-image work, not proof a raw mainline DTB will be accepted.
4. Agree on an observable first boot: captured UART if already available, or a
   separately validated USB gadget path. Do not promise physical UART access.
5. Build a minimal diagnostic kernel/initramfs and only then define the
   pocketboot image contract. Bootloader acceptance, Linux startup, USB,
   storage and kexec each need their own test result.

No unlock, erase, flash, slot-change, or verified-boot-disabling commands are
part of this work. Procedures from PDX206/PDX234 are not assumed to apply.
