# SDM670/PM660 USB-C Type-C and PD Prior Art

Date: 2026-07-08

Scope: distilled local history, downstream evidence, and public-link breadcrumbs for bringing Qualcomm PMIC USB-C role switching and PD negotiation toward mainline on SDM670/PM660 devices such as Google Sargo/Bonito.

## Executive Summary

- Current mainline Sargo USB is high-speed peripheral-only. `arch/arm64/boot/dts/qcom/sdm670-google-common.dtsi` sets `&usb_1_dwc3` to `dr_mode = "peripheral"` and `maximum-speed = "high-speed"`, with the comment `Only peripheral works for now`.
- Mainline has no real Sargo/PM660 Type-C controller node today. `sdm670-google-common-dtbo-mask.dtsi` only exports masked downstream symbols such as `pm660_pdphy` and `usb0`; it does not implement them.
- Mainline PM660 exposes `pm660_charger: charger@1000` only. The charger driver already contains Type-C register definitions and a `FIXME: This will be handled by the type-c driver`, so Type-C register ownership still needs to move out of, or be coordinated with, `qcom_smbx.c`.
- Downstream Google 4.9 provides the strongest hardware evidence: PM660 has a PD PHY at `0x1700` with seven PD IRQs, no `fr-swap` IRQ, and Sargo/Bonito uses DRP with default sink behavior plus an external 5 V boost regulator on PM660 GPIO6 for source VBUS.
- Upstream direction is to use TCPM plus the mainline Qualcomm PMIC Type-C framework in `drivers/usb/typec/tcpm/qcom/`, not downstream extcon/dual-role glue.
- The accepted upstream driver architecture already has separate port and PD PHY backends. Commit `f1a27f081c1f` explicitly says the port-backend split is intended to enable earlier platforms including SDM845, SDM660, and MSM8998.
- The local WIP branches contain useful register/DT experiments for PMI8998, but they are not upstream-ready as-is. The cleaner direction is a PM660/PMI8998 backend inside the existing `qcom_pmic_typec` framework, with bindings for the legacy one-Type-C-IRQ plus seven-PD-IRQ layout.

## Verified Mainline State

- `arch/arm64/boot/dts/qcom/sdm670-google-common.dtsi:1557`:
  - `&usb_1_dwc3` is peripheral-only.
  - `dr_mode = "peripheral"`.
  - `phys = <&usb_1_hsphy>` and `phy-names = "usb2-phy"`.
  - `maximum-speed = "high-speed"`.
- `arch/arm64/boot/dts/qcom/pm660.dtsi:76`:
  - Mainline PM660 has `pm660_charger: charger@1000` with compatible `qcom,pm660-charger`.
  - No PM660 Type-C or PD PHY child exists in mainline.
- `arch/arm64/boot/dts/qcom/sdm670-google-common-dtbo-mask.dtsi:19`:
  - `pm660_pdphy` is only a masked symbol for downstream DTBO compatibility.
- `arch/arm64/boot/dts/qcom/sdm670-google-common.dtsi:727`:
  - `vreg_l7b_3p125` is present and is already used by `usb_1_hsphy` as `vdda-phy-dpdm-supply`; this is the likely mainline counterpart for downstream `pm660l_l7` used by the PD PHY.

## Mainline Qualcomm PMIC Type-C Driver

- Initial accepted driver: `a4422ff221429c600c3dc5d0394fb3738b89d040` (`usb: typec: qcom: Add Qualcomm PMIC Type-C driver`).
- The driver is layered as:
  - `qcom_pmic_typec.c`: TCPM registration and arbitration between Type-C and PD PHY blocks.
  - `qcom_pmic_typec_port.c`: Type-C/CC handling.
  - `qcom_pmic_typec_pdphy.c`: PD PHY handling.
- Current top-level driver supports these runtime match entries in `qcom_pmic_typec.c`:
  - `qcom,pm8150b-typec`
  - `qcom,pmi632-typec`
- Current binding `Documentation/devicetree/bindings/usb/qcom,pmic-typec.yaml` supports:
  - `qcom,pmi632-typec`
  - `qcom,pm8150b-typec`
  - `qcom,pm6150-typec`, fallback `qcom,pm8150b-typec`
  - `qcom,pm7250b-typec`, fallback `qcom,pm8150b-typec`
  - `qcom,pm4125-typec`, fallback `qcom,pmi632-typec`
- Current binding assumptions:
  - PMI632-style no-PD block: one `reg`, max eight interrupts, no `vdd-pdphy-supply`.
  - PM8150B-style PD-capable block: two `reg` entries, 16 interrupts, and required `vdd-pdphy-supply`.
  - The 16th PD IRQ is `fr-swap`.
- This does not directly fit PM660/PMI8998 if they use one Type-C-change IRQ and seven PD PHY IRQs without FRS.

## Accepted History Breadcrumbs

- `00bb478b829e` - `dt-bindings: usb: Add Qualcomm PMIC Type-C`
  - Link trailer: `https://lore.kernel.org/r/20230508142308.1656410-6-bryan.odonoghue@linaro.org`
- `a4422ff22142` - `usb: typec: qcom: Add Qualcomm PMIC Type-C driver`
  - Link trailer: `https://lore.kernel.org/r/20230508142308.1656410-8-bryan.odonoghue@linaro.org`
  - Important because it removed the older non-TCPM QCOM PMIC Type-C detection driver and replaced it with TCPM-based PMIC Type-C plus PD.
- `d2f9b93de0fe` - `usb: typec: qcom-pmic-typec: allow different implementations for the PD PHY`
  - Link trailer: `https://lore.kernel.org/r/20240113-pmi632-typec-v2-7-182d9aa0a5b3@linaro.org`
- `f1a27f081c1f` - `usb: typec: qcom-pmic-typec: allow different implementations for the port backend`
  - Link trailer: `https://lore.kernel.org/r/20240113-pmi632-typec-v2-8-182d9aa0a5b3@linaro.org`
  - Commit text explicitly says the split will later enable earlier platforms, including SDM845, SDM660, and MSM8998.
- `f637c0c6dd81` - `dt-bindings: usb: qcom,pmic-typec: add support for the PMI632 block`
  - Link trailer: `https://lore.kernel.org/r/20240130-pmi632-typec-v3-2-b05fe44f0a51@linaro.org`
- `cf92b9df3dcf` - `usb: typec: qcom-pmic-typec: add support for PMI632 PMIC`
  - Link trailer: `https://lore.kernel.org/r/20240130-pmi632-typec-v3-3-b05fe44f0a51@linaro.org`
  - Important because it introduced a stub PD PHY path for a PMIC with Type-C but no PD PHY.
- `ffe85c24d7ca` - `usb: typec: qcom-pmic-typec: fix sink status being overwritten with RP_DEF`
  - Link trailer: `https://lore.kernel.org/r/20241005144146.2345-1-jonathan@marek.ca`
  - Fixes a TCPM `Sink TX No Go` loop by preserving the actual sink status.

Public lore fetch status: direct web fetches of the lore links above returned Anubis challenge pages. These notes therefore rely on local git commit messages/trailers and do not claim contents from web pages that were not fetched.

## Downstream Google 4.9 Evidence

- `arch/arm64/boot/dts/qcom/pm660.dtsi:247` in the Google 4.9 tree:
  - `pm660_pdphy: qcom,usb-pdphy@1700`
  - `compatible = "qcom,qpnp-pdphy"`
  - `reg = <0x1700 0x100>`
  - `vdd-pdphy-supply = <&pm660l_l7>`
  - Seven PD PHY interrupts only: `sig-tx`, `sig-rx`, `msg-tx`, `msg-rx`, `msg-tx-failed`, `msg-tx-discarded`, `msg-rx-discarded`.
  - No `fr-swap` interrupt.
  - Default sink caps: 5 V at 3 A and 9 V at 3 A.
- `arch/arm64/boot/dts/qcom/sdm670-pmic-overlay.dtsi:377` in the Google 4.9 tree:
  - Overrides `&pm660_pdphy` to use `vbus-supply = <&smb2_vbus>` and `vconn-supply = <&smb2_vconn>`.
  - `&usb0` uses downstream `extcon` wiring and `qcom,no-vbus-vote-with-type-C`.
- `arch/arm64/boot/dts/google/sdm670-b4s4-usb-common.dtsi:31` in the Google 4.9 tree:
  - Defines `ext_5v_boost` as a fixed regulator controlled by PM660 GPIO6.
  - Overrides `&pm660_pdphy` with Google policy:
    - source PDO: 5 V at 900 mA.
    - sink PDOs: 5 V at 3 A and 9 V at 3 A.
    - `goog,port-type = <TYPEC_PORT_DRP>`.
    - `goog,default-role = <TYPEC_SINK>`.
    - `goog,try-role-hw`.
    - `ext-vbus-supply = <&ext_5v_boost>`.
- Mainline cannot reuse downstream `extcon` or Google-specific properties directly. They are evidence for board wiring and policy only.

## Local WIP Branches

### `origin/pmi8998-typec`

- `97d0eb52cea4` - `usb: typec: qcom: add PMI8998 support`
  - Adds `qcom_pmic_typec_port_pmi8998.c` with 953 lines.
  - Commit text says PMI8998 Type-C registers are interspersed with charger registers, unlike PM8150B.
  - Commit text says PD support is left out for now because it was not supported on all platforms and was not tested.
  - Does not update `Documentation/devicetree/bindings/usb/qcom,pmic-typec.yaml` in that commit.
- `3266ecfbf13d` - `arm64: dts: qcom: pmi8998: add vbus and type-c`
  - Adds `pmi8998_vbus: usb-vbus-regulator@1100`.
  - Adds `pmi8998_typec: typec@1300` with `reg = <0x1300>, <0x1700>`.
  - Uses one Type-C IRQ named `type-c-change` plus seven PD PHY IRQs.
- `0afc4fed8cb7` - `arm64: dts: qcom: sdm845-oneplus: enable usb role switching`
  - Notes OnePlus phones only support high-speed USB and have the PD controller disabled in hardware.
- `35a49f3329dd` - `arm64: dts: qcom: sdm845-shift-axolotl: enable type-c`
  - Commit text says PD, role switching, and DP alt mode.

Assessment: useful register and board evidence, but not a good final shape by itself. The large separate PMI8998 port backend and missing binding update are upstreaming risks.

### `sdm845-mainline/caleb/pmi8998-tcpm-next`

- `e0e262cca516` - `usb: typec: qcom: switch to regmap_fields`
  - Refactors the port driver toward register-field indirection.
- `c632a013c0bd` - `WIP: usb: typec: qcom: drop in pmi8998 support`
  - Adds a smaller PMI8998 backend file using regmap fields.
- `8f78befb0372` - `arm64: dts: qcom: pmi8998: define type-c port`
  - Says the Type-C port is intermingled with the charger register block but still encapsulated as its own function.
- `981ad7361199` - `FIXUP: arm64: dts: qcom: pmi8998: remove fr-swap irq`
  - Commit text says PMI8998 does not support fast role swap.

Assessment: better architectural idea than the 953-line separate backend because it abstracts register fields, but this branch is explicitly WIP/debug-heavy and would need cleanup before upstreaming.

## Charger Driver Interaction

- `drivers/power/supply/qcom_smbx.c` is for PMI8998 and related switch-mode battery charger/boost hardware.
- It defines PM660/PMI8998-era Type-C registers including:
  - `TYPE_C_STATUS_1` through `TYPE_C_STATUS_5`.
  - `TYPE_C_CFG`, `TYPE_C_CFG_2`, `TYPE_C_CFG_3`.
  - `TYPE_C_INTRPT_ENB_SOFTWARE_CTRL`.
  - `USBIN_CURRENT_LIMIT_CFG` and `ICL_STATUS`.
- `smb_init_seq` currently configures Type-C state because there is no Type-C owner yet:
  - Comment: `By default configure us as an upstream facing port`.
  - Comment: `FIXME: This will be handled by the type-c driver`.
- `qcom_smbx.c` already supports writable `POWER_SUPPLY_PROP_CURRENT_MAX` through `smb_set_current_limit()`.
- Full PD sink support should coordinate TCPM negotiated current/voltage with charger current-limit programming. Otherwise PD negotiation can succeed while charger input current remains stuck at APSD/SDP/CDP/DCP defaults.

## Mainlinable Shape

- Keep using `drivers/usb/typec/tcpm/qcom/` and TCPM.
- Do not revive downstream `extcon`, `dual_role_usb`, or Google-specific `goog,*` policy properties.
- Add PM660/PMI8998 support as another port backend under `qcom_pmic_typec`, reusing the accepted backend split from `f1a27f081c1f`.
- Reuse the existing PD PHY backend if the PM660/PMI8998 `0x1700` PD PHY register layout matches; adjust only for `nr_irqs = 7` and no FRS IRQ if needed.
- Add binding support for the legacy layout:
  - New compatibles likely `qcom,pm660-typec` and `qcom,pmi8998-typec`.
  - `reg = <0x1300>, <0x1700>` is supported by local PMI8998 WIP and by the charger register offsets in `qcom_smbx.c`.
  - Interrupt layout is one Type-C change IRQ plus seven PD PHY IRQs, not PM8150B's eight Type-C IRQs plus eight PD PHY IRQs.
  - Binding must not require `fr-swap` for PM660/PMI8998.
- Model the connector with the standard `usb-c-connector` schema:
  - For a first HS-only/no-PD bring-up, `pd-disable` and `typec-power-opmode` are valid connector-schema tools.
  - For full PD, use standard `source-pdos`, `sink-pdos`, `op-sink-microwatt`, `power-role`, `try-power-role`, and `data-role` rather than downstream `goog,*` properties.
- Use native DWC3 `usb-role-switch` and graph endpoints for role switching.

## Suggested Bring-Up Sequence

1. HS-only Type-C DRD first:
   - Add PM660/PMI8998 Type-C port backend enough for CC attach/orientation and data-role switching.
   - Use `usb-role-switch` and change Sargo `&usb_1_dwc3` from peripheral-only to OTG.
   - Keep `maximum-speed = "high-speed"` initially.
   - Use `pd-disable` until PD PHY and charger-current behavior are verified.
   - Model `ext_5v_boost` from downstream PM660 GPIO6 if hardware validation confirms it is the board's source VBUS path.
2. Add PD PHY reuse:
   - Wire PM660 `typec@1300` with PD PHY `0x1700` and seven PD interrupts.
   - Validate hard reset, transmit, receive, and sink/source negotiation.
   - Do not invent an FRS IRQ.
3. Add charger coordination:
   - Connect TCPM negotiated current limits to `qcom_smbx` or another mainline power-supply path.
   - Ensure APSD current-limit updates do not overwrite negotiated PD limits incorrectly.
   - Decide how VBUS source enable is owned: PMIC OTG regulator, external boost, or both, based on measured Sargo hardware behavior.
4. Only then enable full PD PDOs in Sargo DT.

## Open Questions

- Does Sargo source VBUS exclusively through the external PM660 GPIO6 fixed regulator, through SMB2 OTG, or through both in different modes?
- Does PM660's Type-C-change IRQ at peripheral `0x13`, interrupt `0x07`, match PMI8998's WIP DTS and mainline `qcom_smbx.c` assumptions on real Sargo hardware?
- Can the existing mainline PD PHY backend handle a seven-IRQ PM660 PD PHY by changing resource data only, or does it need small conditional logic around FRS?
- How should `qcom_smbx` and `qcom_pmic_typec` share or transfer ownership of Type-C registers currently initialized by the charger driver?
- Where should negotiated PD current limit be applied so that TCPM, APSD, and thermal/current-limit policy do not fight each other?
- What userspace is expected on the target rootfs for gadget functions when the kernel switches into device role?

## Userspace Notes

- Mainline TCPM should own CC/PD state and role swaps in-kernel.
- Userspace still needs gadget setup for device mode, usually through configfs and a system service.
- Desktop or embedded userspace may watch `/sys/class/typec`, `/sys/class/usb_role`, and `/sys/class/power_supply` for UI/policy, but it should not act as a PD policy engine.
