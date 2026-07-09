# Sargo Type-C Findings

This is a consolidated dump of the Sargo/Bonito USB-C/Type-C investigation.

The goal was to understand how Google's downstream Sargo Type-C implementation and board wiring work, identify the best GPL/community sources to trust, and map that onto a mainline-friendly implementation path for SDM670/PM660.

## Current Mainline State

Repository inspected: `/var/home/sam/tmp/linux-sdm670-mainline`

Current branch at the start of the investigation:

```text
sdm670-mainline-on-stable
```

Current HEAD at the start of the investigation:

```text
75d5a98752026 (HEAD -> sdm670-mainline-on-stable, tag: sdm670-v7.1.3_beta1, sdm670-mainline/on-stable, sdm670-mainline/HEAD)
```

Current mainline-ish Sargo USB state:

```dts
&usb_1_dwc3 {
        /* Only peripheral works for now */
        dr_mode = "peripheral";
        maximum-speed = "high-speed";
};
```

Important current limitations:

- Sargo is HS peripheral-only today.
- No `usb-role-switch` wiring is present.
- No `dr_mode = "otg"` wiring is present.
- No standard `usb-c-connector` node is present.
- No real mainline `pm660_pdphy` or PM660 Type-C node is present.
- `arch/arm64/boot/dts/qcom/sdm670-google-common-dtbo-mask.dtsi` only masks downstream symbols such as `pm660_pdphy`, `usb0`, and `eud`; it does not implement them.

Relevant current files:

- `arch/arm64/boot/dts/qcom/sdm670-google-sargo.dts`
- `arch/arm64/boot/dts/qcom/sdm670-google-common.dtsi`
- `arch/arm64/boot/dts/qcom/sdm670.dtsi`
- `arch/arm64/boot/dts/qcom/sdm670-google-common-dtbo-mask.dtsi`
- `arch/arm64/boot/dts/qcom/pm660.dtsi`
- `arch/arm64/boot/dts/qcom/pm660l.dtsi`

## Sources Added And Fetched

These remotes were added in `/var/home/sam/tmp/linux-sdm670-mainline` during the investigation.

Kernel remotes:

```text
aosp-msm                  https://android.googlesource.com/kernel/msm
lineage-google-msm49      https://github.com/LineageOS/android_kernel_google_msm-4.9.git
pixelexp-google-msm49     https://github.com/PixelExperience-Devices/kernel_google_msm-4.9.git
```

Device/userspace remotes:

```text
aosp-device-bonito            https://android.googlesource.com/device/google/bonito
aosp-device-bonito-sepolicy   https://android.googlesource.com/device/google/bonito-sepolicy
lineage-device-bonito         https://github.com/LineageOS/android_device_google_bonito.git
lineage-device-sargo          https://github.com/LineageOS/android_device_google_sargo.git
```

Fetched AOSP kernel branch tips:

```text
aosp-msm/android-msm-bonito-4.9-android10
aosp-msm/android-msm-bonito-4.9-android10-qpr1
aosp-msm/android-msm-bonito-4.9-android10-qpr2
aosp-msm/android-msm-bonito-4.9-android10-qpr3
aosp-msm/android-msm-bonito-4.9-android10-release
aosp-msm/android-msm-bonito-4.9-android10-s1
aosp-msm/android-msm-bonito-4.9-android11
aosp-msm/android-msm-bonito-4.9-android11-qpr1
aosp-msm/android-msm-bonito-4.9-android11-qpr2
aosp-msm/android-msm-bonito-4.9-android11-qpr3
aosp-msm/android-msm-bonito-4.9-android12
aosp-msm/android-msm-bonito-4.9-android12-qpr1
aosp-msm/android-msm-bonito-4.9-android12-v2-beta-2
aosp-msm/android-msm-bonito-4.9-android12L
aosp-msm/android-msm-bonito-4.9-pie-b4s4
aosp-msm/android-msm-bonito-4.9-pie-qpr3-b
aosp-msm/android-msm-bonito-4.9-q-preview-3
aosp-msm/android-msm-bonito-4.9-q-preview-4
aosp-msm/android-msm-bonito-4.9-q-preview-5
aosp-msm/android-msm-bonito-4.9-q-preview-6
aosp-msm/android-msm-bonito-4.9-r-beta-1
aosp-msm/android-msm-bonito-4.9-r-beta-2
aosp-msm/android-msm-bonito-4.9-r-beta-3
aosp-msm/android-msm-bonito-4.9-r-preview-1
aosp-msm/android-msm-bonito-4.9-r-preview-2
aosp-msm/android-msm-bonito-4.9-r-preview-3
aosp-msm/android-msm-bonito-4.9-r-preview-4
aosp-msm/android-msm-bonito-4.9-s-beta-2
aosp-msm/android-msm-bonito-4.9-s-beta-3
aosp-msm/android-msm-bonito-4.9-s-beta-4
aosp-msm/android-msm-bonito-4.9-s-beta-5
aosp-msm/android-msm-bonito-4.9-s-preview-1
aosp-msm/android-msm-bonito-4.9-s-preview-2
aosp-msm/android-msm-bonito-4.9-s-preview-3
aosp-msm/android-msm-bonito-4.9-s-v2-beta-1
aosp-msm/android-msm-bonito-4.9-s-v2-beta-3
```

Fetched community kernel branch tips:

```text
lineage-google-msm49/lineage-17.0
lineage-google-msm49/lineage-17.1
lineage-google-msm49/lineage-18.0
lineage-google-msm49/lineage-18.1
lineage-google-msm49/lineage-19.0
lineage-google-msm49/lineage-19.1
lineage-google-msm49/lineage-20
lineage-google-msm49/lineage-21
lineage-google-msm49/lineage-22.0
lineage-google-msm49/lineage-22.1
lineage-google-msm49/lineage-22.2
pixelexp-google-msm49/eleven
pixelexp-google-msm49/twelve
pixelexp-google-msm49/thirteen
pixelexp-google-msm49/thirteen-old
pixelexp-google-msm49/thirteen-plus
pixelexp-google-msm49/thirteen-wip
```

Fetched device/userspace branch tips:

```text
aosp-device-bonito/pie-b4s4-release
aosp-device-bonito/android10-qpr3-release
aosp-device-bonito/android12L-release
aosp-device-bonito-sepolicy/pie-b4s4-release
aosp-device-bonito-sepolicy/android10-qpr3-release
aosp-device-bonito-sepolicy/android12L-release
lineage-device-bonito/lineage-22.2
lineage-device-bonito/lineage-23.0
lineage-device-sargo/lineage-22.2
lineage-device-sargo/lineage-23.0
```

## Source Priority

Best sources for kernel wiring:

- `aosp-msm/android-msm-bonito-4.9-android12L`
- `aosp-msm/android-msm-bonito-4.9-pie-b4s4`
- `lineage-google-msm49/lineage-22.2` for cross-checking, not as the primary authority.
- `pixelexp-google-msm49/thirteen` for additional cross-checking, not as the primary authority.

Best sources for userspace behavior:

- `aosp-device-bonito/android12L-release`
- `aosp-device-bonito-sepolicy/android12L-release`
- `lineage-device-bonito/lineage-22.2`

Important caveat:

- `lineage-device-sargo` is only a stub. It says Sargo device files live in `device/google/bonito`.

## Stable Downstream Wiring Findings

The most important downstream Sargo/Bonito board file is:

```text
aosp-msm/android-msm-bonito-4.9-android12L:arch/arm64/boot/dts/google/sdm670-b4s4-usb-common.dtsi
```

This file shows the shared Sargo/Bonito USB-C board-specific wiring.

Key board-level pieces:

- PM660 GPIO6 drives `ext_5v_boost`.
- `ext_5v_boost` is a fixed regulator.
- `ext_5v_boost` is used as `ext-vbus-supply` for `pm660_pdphy`.
- TLMM GPIO21 is configured as `cc_sbu_ovp_intr` pinctrl input.
- `usb0` has `google,switch-vbus = <250>`.
- DWC3 is forced to `maximum-speed = "high-speed"`.
- `usb_qmp_dp_phy` is disabled.

Relevant downstream snippet:

```dts
&pm660_gpios {
        usb2_ext_5v_boost {
                usb2_ext_5v_boost_default: usb2_ext_5v_boost_default {
                        pins = "gpio6";
                        function = PMIC_GPIO_FUNC_NORMAL;
                        output-low;
                        power-source = <0>;
                };
        };
};

&vendor {
        ext_5v_boost: ext_5v_boost {
                compatible = "regulator-fixed";
                regulator-name = "ext_5v_boost";
                gpio = <&pm660_gpios 6 GPIO_ACTIVE_HIGH>;
                enable-active-high;
                regulator-enable-ramp-delay = <1600>;
                pinctrl-names = "default";
                pinctrl-0 = <&usb2_ext_5v_boost_default>;
        };
};
```

Downstream Type-C policy on `pm660_pdphy`:

```dts
&pm660_pdphy {
        goog,src-pdo = <PDO_TYPE_FIXED 5000 900 0>;     /* 5V @ 0.9A */
        goog,snk-pdo = <PDO_TYPE_FIXED 5000 3000 0>,    /* 5V @ 3A */
                       <PDO_TYPE_FIXED 9000 3000 0>;    /* 9V @ 3A */
        goog,max-snk-mv = <9000>;
        goog,max-snk-ma = <3000>;
        goog,max-snk-mw = <27000>;
        goog,op-snk-mw = <2500>;
        goog,port-type = <TYPEC_PORT_DRP>;
        goog,default-role = <TYPEC_SINK>;
        goog,try-role-hw;

        ext-vbus-supply = <&ext_5v_boost>;

        pinctrl-names = "default";
        pinctrl-0 = <&cc_sbu_ovp_intr_default>;
};
```

The original `pie-b4s4` value was:

```dts
goog,op-snk-mw = <7600>;
```

By `android12L`, Google changed only that board policy value to:

```dts
goog,op-snk-mw = <2500>;
```

The actual board wiring, PDO maxima, default role, DRP policy, and external VBUS path stayed stable across the fetched Google AOSP kernel line.

## PM660 PD PHY

The PM660 PD PHY is defined in:

```text
aosp-msm/android-msm-bonito-4.9-android12L:arch/arm64/boot/dts/qcom/pm660.dtsi
```

Key downstream node:

```dts
pm660_pdphy: qcom,usb-pdphy@1700 {
        compatible = "qcom,qpnp-pdphy";
        reg = <0x1700 0x100>;
        vdd-pdphy-supply = <&pm660l_l7>;
        vbus-supply = <0>;
        vconn-supply = <0>;
        interrupts = <0x0 0x17 0x0 IRQ_TYPE_EDGE_RISING>,
                     <0x0 0x17 0x1 IRQ_TYPE_EDGE_RISING>,
                     <0x0 0x17 0x2 IRQ_TYPE_EDGE_RISING>,
                     <0x0 0x17 0x3 IRQ_TYPE_EDGE_RISING>,
                     <0x0 0x17 0x4 IRQ_TYPE_EDGE_RISING>,
                     <0x0 0x17 0x5 IRQ_TYPE_EDGE_RISING>,
                     <0x0 0x17 0x6 IRQ_TYPE_EDGE_RISING>;

        interrupt-names = "sig-tx",
                          "sig-rx",
                          "msg-tx",
                          "msg-rx",
                          "msg-tx-failed",
                          "msg-tx-discarded",
                          "msg-rx-discarded";
};
```

Mainline implication:

- Mainline `drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_pdphy.c` likely can be reused for PM660 with minimal or no register changes.
- The IRQ names and PD PHY offset line up with existing Qualcomm PMIC TCPM expectations.
- PM660/PMI8998 do not appear to have the newer PM8150B-style FRS IRQ layout.

## PM660 Charger And Type-C Registers

The PM660 Type-C IRQ source comes from the charger USB charge path at effective offset `0x1300`.

Downstream charger/PMIC overlay:

```text
aosp-msm/android-msm-bonito-4.9-android12L:arch/arm64/boot/dts/qcom/sdm670-pmic-overlay.dtsi
```

Relevant downstream node:

```dts
qcom,usb-chgpth@1300 {
        reg = <0x1300 0x100>;
        interrupts =
                <0x0 0x13 0x0 IRQ_TYPE_EDGE_BOTH>,
                <0x0 0x13 0x1 IRQ_TYPE_EDGE_BOTH>,
                <0x0 0x13 0x2 IRQ_TYPE_EDGE_BOTH>,
                <0x0 0x13 0x3 IRQ_TYPE_EDGE_BOTH>,
                <0x0 0x13 0x4 IRQ_TYPE_EDGE_BOTH>,
                <0x0 0x13 0x5 IRQ_TYPE_EDGE_RISING>,
                <0x0 0x13 0x6 IRQ_TYPE_EDGE_RISING>,
                <0x0 0x13 0x7 IRQ_TYPE_EDGE_RISING>;

        interrupt-names = "usbin-collapse",
                          "usbin-lt-3p6v",
                          "usbin-uv",
                          "usbin-ov",
                          "usbin-plugin",
                          "usbin-src-change",
                          "usbin-icl-change",
                          "type-c-change";
};
```

Mainline implication:

- A mainline PM660 Type-C TCPM node probably needs `reg = <0x1300>, <0x1700>`.
- The Type-C port backend needs the single legacy Type-C IRQ `type-c-change` from `0x13/7`.
- The PD PHY backend needs the seven PD PHY IRQs from `0x17/0..6`.

Downstream PM660/SMB2 also defines VBUS/VCONN regulators:

```dts
smb2_vbus: qcom,smb2-vbus {
        regulator-name = "smb2-vbus";
};

smb2_vconn: qcom,smb2-vconn {
        regulator-name = "smb2-vconn";
};

&pm660_pdphy {
        vbus-supply = <&smb2_vbus>;
        vconn-supply = <&smb2_vconn>;
};
```

Sargo/Bonito board DT then adds external source VBUS:

```dts
ext-vbus-supply = <&ext_5v_boost>;
```

Interpretation:

- SMB2 VBUS/VCONN are part of the generic PM660 charger/Type-C supply model.
- Sargo/Bonito additionally have an external PM660 GPIO6 controlled 5V boost for source VBUS.
- Downstream policy can switch between internal/external VBUS paths; mainline needs hardware validation before confidently modeling both paths.

## Downstream Driver References

Important downstream driver files:

```text
aosp-msm/android-msm-bonito-4.9-android12L:drivers/usb/pd/pd_engine.c
aosp-msm/android-msm-bonito-4.9-android12L:drivers/usb/pd/policy_engine.c
aosp-msm/android-msm-bonito-4.9-android12L:drivers/usb/pd/qpnp-pdphy.c
aosp-msm/android-msm-bonito-4.9-android12L:drivers/power/supply/qcom/smb-lib.c
aosp-msm/android-msm-bonito-4.9-android12L:drivers/power/supply/qcom/smb-reg.h
aosp-msm/android-msm-bonito-4.9-android12L:drivers/power/supply/qcom/qpnp-typec.c
```

Downstream `pd_engine.c` behavior:

- Reads `goog,snk-pdo`.
- Reads `goog,src-pdo`.
- Reads `goog,port-type`.
- Reads `goog,default-role`.
- Reads `goog,try-role-hw`.
- Gets `vbus`, `vconn`, and `ext-vbus` regulators.
- Registers/extcon-connects downstream Android role handling.
- Calls charger power-supply APIs for `POWER_SUPPLY_PROP_TYPEC_POWER_ROLE`.
- Disables/enables APSD and USB input current limit around source VBUS transitions.
- Tracks `external_vbus` and `external_vbus_update`.
- Chooses external VBUS in part based on `wireless_online`.

Downstream Type-C/SMB register breadcrumbs:

```text
drivers/power/supply/qcom/smb-reg.h
TYPE_C_STATUS_1_REG                    (USBIN_BASE + 0x0B)
TYPE_C_CFG_REG                         (USBIN_BASE + 0x58)
TYPE_C_CFG_2_REG                       (USBIN_BASE + 0x59)
TYPE_C_CFG_3_REG                       (USBIN_BASE + 0x5A)
TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG    (USBIN_BASE + 0x68)
TYPEC_POWER_ROLE_CMD_MASK              GENMASK(2, 0)
```

Current mainline also has PM660/PMI8998 Type-C breadcrumbs in:

```text
drivers/power/supply/qcom_smbx.c
```

Relevant mainline breadcrumbs:

```text
TYPE_C_STATUS_1 0x30B
TYPE_C_STATUS_2 0x30C
TYPE_C_STATUS_3 0x30D
TYPE_C_STATUS_4 0x30E
TYPE_C_STATUS_5 0x30F
```

Important mainline comment in `qcom_smbx.c`:

```text
FIXME: This will be handled by the type-c driver
```

Interpretation:

- PM660/PMI8998 Type-C detection/control registers are charger-relative.
- The effective Type-C port block is the charger USB path at `0x1300`.
- Mainline should not keep Type-C logic in the charger driver long-term.

## Mainline Qualcomm PMIC TCPM Stack

Mainline Qualcomm PMIC TCPM files inspected:

```text
drivers/usb/typec/tcpm/qcom/qcom_pmic_typec.c
drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_port.c
drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_pdphy.c
drivers/usb/typec/tcpm/qcom/qcom_pmic_typec.h
Documentation/devicetree/bindings/usb/qcom,pmic-typec.yaml
```

Current mainline binding supports newer PMICs such as:

- `qcom,pmi632-typec`
- `qcom,pm8150b-typec`
- PM6150/PM7250B style fallbacks
- PM4125 fallback

Current binding issue:

- `qcom,pmic-typec.yaml` assumes a PM8150B-style PD-capable interrupt layout with multiple Type-C port IRQs plus PD PHY IRQs.
- PM660/PMI8998 need an older interrupt shape: one Type-C IRQ named `type-c-change` plus seven PD PHY IRQs.
- PM660/PMI8998 likely have no FRS IRQ.

## WIP Branches Inspected

Useful but not directly upstream-ready branch:

```text
origin/pmi8998-typec
```

Relevant commits from that branch:

```text
97d0eb52cea42 usb: typec: qcom: add PMI8998 support
3266ecfbf13d8 arm64: dts: qcom: pmi8998: add vbus and type-c
0afc4fed8cb7e arm64: dts: qcom: sdm845-oneplus: enable usb role switching
35a49f3329dd2 arm64: dts: qcom: sdm845-shift-axolotl: enable type-c
```

Problems with `origin/pmi8998-typec`:

- Adds a 953-line `qcom_pmic_typec_port_pmi8998.c` backend.
- Selects backend using `device_is_compatible(dev, "qcom,pmi8998-typec")`.
- Does not update `Documentation/devicetree/bindings/usb/qcom,pmic-typec.yaml`.
- DTS interrupt shape does not match current schema.
- It is useful reference code, but not a clean upstream direction.

Other WIP branches inspected:

```text
sdm845-mainline/caleb/pmi8998-tcpm
sdm845-mainline/caleb/pmi8998-tcpm-next
```

Takeaway from those branches:

- They are debug/WIP-heavy.
- `pmi8998-tcpm-next` shows a better direction: one generic port engine with PM8150B vs PMI8998/PM660 register-field tables.
- That register-field abstraction is a useful design reference, but should not be copied blindly.

## Android Userspace Findings

Important AOSP device files:

```text
aosp-device-bonito/android12L-release:init.hardware.rc
aosp-device-bonito/android12L-release:init.hardware.usb.rc
aosp-device-bonito/android12L-release:usb/Usb.cpp
aosp-device-bonito/android12L-release:usb/UsbGadget.cpp
aosp-device-bonito/android12L-release:usb/android.hardware.usb@1.3-service.bonito.rc
```

Important LineageOS device files:

```text
lineage-device-bonito/lineage-22.2:usb/usb/Usb.cpp
lineage-device-bonito/lineage-22.2:usb/usb/Usb.h
lineage-device-bonito/lineage-22.2:usb/gadget/UsbGadget.cpp
lineage-device-bonito/lineage-22.2:usb/gadget/android.hardware.usb.gadget-service.bonito.rc
```

Android 12L init writes:

```rc
write /sys/class/typec/port0/port_type sink
```

Interpretation:

- The kernel DT declares DRP/default sink.
- Android userspace forces the exposed port type to `sink` during boot.
- This is likely Android product policy and should not be mistaken for board wiring.
- Mainline Linux should normally let TCPM and desktop policy handle role policy unless a real hardware constraint is found.

Android/Lineage USB HAL permissions:

```rc
chown root system /sys/class/typec/port0/power_role
chown root system /sys/class/typec/port0/data_role
chown root system /sys/class/typec/port0/port_type
chmod 664 /sys/class/typec/port0/power_role
chmod 664 /sys/class/typec/port0/data_role
chmod 664 /sys/class/typec/port0/port_type
```

Android/Lineage USB HAL uses standard Type-C sysfs:

```text
/sys/class/typec
/sys/class/typec/<port>/power_role
/sys/class/typec/<port>/data_role
/sys/class/typec/<port>/port_type
/sys/class/typec/<port>-partner/accessory_mode
/sys/class/typec/<port>-partner/supports_usb_power_delivery
```

LineageOS `Usb.cpp` role switching policy:

- It checks `<port>-partner/supports_usb_power_delivery`.
- It only advertises role switching when the partner reports `supports_usb_power_delivery=yes`.
- That is Android HAL policy, not a kernel requirement.
- It does mean proper PD partner reporting mattered to the userspace stack.

LineageOS `Usb.h` comment:

```text
The type-c stack waits for 4.5 - 5.5 secs before declaring a port non-pd.
The -partner directory would not be created until this is done.
Having a margin of ~3 secs for the directory and other related bookeeping
structures created and uvent fired.
```

Gadget userspace:

```text
GADGET_NAME = "a600000.dwc3"
PULLUP_PATH = "/config/usb_gadget/g1/UDC"
```

Android init sets:

```rc
setprop sys.usb.controller "a600000.dwc3"
```

Mainline implication:

- The kernel should expose standard Type-C and USB role interfaces.
- Userspace still needs configfs gadget setup for device mode.
- Android-specific HAL behavior is useful evidence, but mainline should not clone Android init or HAL policy into DT.

## SELinux And Sysfs Breadcrumbs

Important SELinux files:

```text
aosp-device-bonito-sepolicy/android12L-release:vendor/google/genfs_contexts
aosp-device-bonito-sepolicy/android12L-release:vendor/qcom/sdm710/genfs_contexts
aosp-device-bonito-sepolicy/android12L-release:vendor/qcom/common/hal_usb_impl.te
lineage-device-bonito/lineage-22.2:sepolicy/vendor/google/genfs_contexts
lineage-device-bonito/lineage-22.2:sepolicy/vendor/qcom/sdm710/genfs_contexts
```

SELinux labels standard Type-C class paths:

```text
genfscon sysfs /class/typec u:object_r:sysfs_usb_c:s0
genfscon sysfs /class/typec/usbc0 u:object_r:sysfs_usb_c:s0
```

SELinux labels downstream PM660 PD PHY Type-C path:

```text
/devices/platform/soc/c440000.qcom,spmi/spmi-0/spmi0-00/c440000.qcom,spmi:qcom,pm660@0:qcom,usb-pdphy@1700/usbpd0/typec
```

Debug paths referenced:

```text
/tcpm/usbpd0
/logbuffer/usbpd
```

LineageOS additionally labels the downstream extcon path under PM660 PD PHY:

```text
/devices/platform/soc/c440000.qcom,spmi/spmi-0/spmi0-00/c440000.qcom,spmi:qcom,pm660@0:qcom,usb-pdphy@1700/extcon
```

Mainline implication:

- Downstream created a Type-C class device under the PD PHY device.
- Mainline Qualcomm PMIC TCPM should create standard `/sys/class/typec` and `/sys/class/usb_role` instead of downstream extcon/dual_role plumbing.

## Current Mainline Architecture Direction

Do not revive downstream architecture:

- Do not revive downstream extcon as the primary interface.
- Do not revive Android `dual_role_usb` as the primary interface.
- Do not move Type-C policy into the charger driver.
- Do not copy Google-specific `goog,*` policy properties into mainline DT.

Use mainline architecture:

- Extend `drivers/usb/typec/tcpm/qcom/`.
- Use TCPM for Type-C/PD state and role policy.
- Use `usb-role-switch` for DWC3 role control.
- Use a standard `usb-c-connector` DT node.
- Use standard connector PDO properties.
- Use regulator supplies for VBUS/VCONN.
- Keep charger-current integration via power-supply APIs as a later/explicit piece.

Relevant mainline DWC3 behavior:

- `usb-role-switch` disables extcon lookup in `drivers/usb/dwc3/core.c`.
- DWC3 role switch registration happens in `drivers/usb/dwc3/drd.c`.

## Proposed Mainline Implementation Path

Phase 1: PM660/PMI8998 binding support.

- Update `Documentation/devicetree/bindings/usb/qcom,pmic-typec.yaml`.
- Add `qcom,pm660-typec`.
- Add `qcom,pmi8998-typec` if implementing both together.
- Model legacy interrupt layout: one Type-C IRQ named `type-c-change` plus seven PD PHY IRQs.
- Avoid requiring PM8150B-style FRS IRQs for PM660/PMI8998.

Phase 2: PM660/PMI8998 legacy Type-C port backend.

- Extend `drivers/usb/typec/tcpm/qcom/`.
- Prefer one backend with register/resource tables over a second 950-line near-copy backend.
- Use downstream `smb-lib.c`, `smb-reg.h`, `qpnp-typec.c`, and WIP PMI8998 backend as register references.
- Consider the `pmi8998-tcpm-next` register-field abstraction as a design hint only.

Phase 3: Reuse or minimally extend PD PHY backend.

- Reuse `qcom_pmic_typec_pdphy.c` for PM660 `0x1700` if possible.
- Add resource data only if needed.
- Keep PM660/PMI8998 no-FRS layout separate from PM8150B assumptions.

Phase 4: Sargo/Bonito DT enablement.

- Add PM660 Type-C/PD node, likely `typec@1300` with `reg = <0x1300>, <0x1700>`.
- Add Type-C IRQ `type-c-change` from PM660 USB charge path.
- Add seven PD PHY IRQs from `0x17/0..6`.
- Add `vdd-pdphy-supply = <&vreg_l7b_3p125>` or the correct mainline PM660L L7 regulator label.
- Add standard `usb-c-connector` child.
- Add connector PDOs equivalent to downstream policy.
- Add DWC3 endpoint to connector endpoint.
- Set DWC3 `dr_mode = "otg"`.
- Add `usb-role-switch`.
- Keep `maximum-speed = "high-speed"` initially if SS/DP PHY is not supported.

Phase 5: Source VBUS model.

- First-pass safe candidate: model `ext_5v_boost` as the source VBUS regulator.
- Use PM660 GPIO6 pinctrl and `regulator-fixed`.
- Convert downstream `regulator-enable-ramp-delay = <1600>` to mainline `startup-delay-us = <1600>` if appropriate.
- Do not model both SMB2 OTG VBUS and external boost as active source paths without hardware validation.

Phase 6: Charger-current integration.

- Avoid advertising aggressive PD sink behavior until charger-current negotiation is verified.
- Mainline `qcom_smbx.c` exposes writable `POWER_SUPPLY_PROP_CURRENT_MAX`; this may be usable for TCPM `set_current_limit` later.
- Downstream does more than just set current: it coordinates APSD, USB ICL votables, and power-role state through charger APIs.

## Draft Mainline DT Shape

This is a sketch, not a validated patch.

```dts
&pm660_0 {
        pm660_typec: typec@1300 {
                compatible = "qcom,pm660-typec";
                reg = <0x1300>, <0x1700>;

                interrupts = <0x0 0x13 0x7 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x0 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x1 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x2 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x3 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x4 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x5 IRQ_TYPE_EDGE_RISING>,
                             <0x0 0x17 0x6 IRQ_TYPE_EDGE_RISING>;

                interrupt-names = "type-c-change",
                                  "sig-tx",
                                  "sig-rx",
                                  "msg-tx",
                                  "msg-rx",
                                  "msg-tx-failed",
                                  "msg-tx-discarded",
                                  "msg-rx-discarded";

                vdd-pdphy-supply = <&vreg_l7b_3p125>;
                vdd-vbus-supply = <&ext_5v_boost>;

                connector {
                        compatible = "usb-c-connector";
                        label = "USB-C";
                        data-role = "dual";
                        power-role = "dual";
                        try-power-role = "sink";

                        source-pdos = <PDO_FIXED(5000, 900, PDO_FIXED_USB_COMM)>;
                        sink-pdos = <PDO_FIXED(5000, 3000, PDO_FIXED_USB_COMM)>,
                                    <PDO_FIXED(9000, 3000, PDO_FIXED_USB_COMM)>;
                        op-sink-microwatt = <2500000>;

                        ports {
                                port@0 {
                                        reg = <0>;
                                        pm660_typec_hs: endpoint {
                                                remote-endpoint = <&usb_1_dwc3_hs>;
                                        };
                                };
                        };
                };
        };
};

&usb_1_dwc3 {
        dr_mode = "otg";
        maximum-speed = "high-speed";
        usb-role-switch;
};
```

Open questions in that sketch:

- Exact mainline regulator label for PM660L L7.
- Exact binding supply names for reused Qualcomm PMIC TCPM backend.
- Whether `vdd-vbus-supply` should point at external boost, SMB2 VBUS, or a more explicit combined/conditional model.
- Whether source PDO should include `PDO_FIXED_USB_COMM` flags exactly as shown.
- Whether full PD should be initially disabled until charger-current integration is validated.

## Userspace Requirements For Mainline

Kernel support alone is not enough for a useful device-mode experience.

Needed userspace pieces:

- A configfs gadget service for device mode.
- A policy daemon or desktop integration for `/sys/class/typec`.
- Role-switch awareness through `/sys/class/usb_role`.
- Power-supply monitoring through `/sys/class/power_supply`.
- Optional policy around automatic device/host switching.

For Android-like behavior:

- Android uses a USB HAL that talks to `/sys/class/typec`.
- Android configures configfs gadget functions under `/config/usb_gadget/g1`.
- Android expects the UDC name `a600000.dwc3`.

For mainline Linux:

- Use standard Linux Type-C, USB role switch, configfs gadget, and power-supply interfaces.
- Do not require Android's downstream extcon or `dual_role_usb` ABI.

## Remaining Hardware Unknowns

VBUS source path still needs hardware validation:

- Is Sargo source VBUS always external PM660 GPIO6 boost?
- Does SMB2 OTG VBUS participate in source mode on Sargo?
- Does downstream switch between SMB2 VBUS and external boost only for wireless charging scenarios?
- Can PM660 GPIO6 boost safely be enabled as the sole mainline source VBUS regulator?

Charger-current integration still needs validation:

- Can mainline `qcom_smbx` current-limit property handle PD negotiated current correctly?
- Is a TCPM `set_current_limit` hook needed immediately?
- What happens with 9V/3A sink PDO without the downstream charger/votable machinery?

PD enablement strategy still needs validation:

- Safe phase 1 may be HS-only Type-C DRD with no full PD sink overclaim.
- Full PD sink/source behavior should follow after charger-current and VBUS handling are proven.

## Recommended Next Step

The safest next implementation step is not to copy downstream wholesale.

Recommended first patch series shape:

```text
1. dt-bindings: usb: qcom,pmic-typec: add PM660/PMI8998 legacy interrupt layout
2. usb: typec: qcom: add PM660/PMI8998 legacy Type-C port backend/resources
3. arm64: dts: qcom: pm660: add Type-C/PD node or board-level equivalent
4. arm64: dts: qcom: sdm670-google-common/sargo: add HS-only USB-C DRD wiring
```

Recommended implementation posture:

- Keep the first driver patch minimal.
- Prefer a register table/resource abstraction over a separate huge copied backend.
- Reuse the existing PD PHY backend if possible.
- Model external boost VBUS conservatively.
- Do not enable full 9V/3A PD sink behavior until charger-current handling is proven.

## Useful Commands

Show the downstream Sargo/Bonito USB-C board file:

```sh
git show aosp-msm/android-msm-bonito-4.9-android12L:arch/arm64/boot/dts/google/sdm670-b4s4-usb-common.dtsi
```

Show the PM660 PD PHY node:

```sh
git show aosp-msm/android-msm-bonito-4.9-android12L:arch/arm64/boot/dts/qcom/pm660.dtsi
```

Show the PM660 charger/USB charge path overlay:

```sh
git show aosp-msm/android-msm-bonito-4.9-android12L:arch/arm64/boot/dts/qcom/sdm670-pmic-overlay.dtsi
```

Compare the earliest and latest Google board-level USB-C policy:

```sh
git diff aosp-msm/android-msm-bonito-4.9-pie-b4s4 aosp-msm/android-msm-bonito-4.9-android12L -- arch/arm64/boot/dts/google/sdm670-b4s4-usb-common.dtsi
```

Search the fetched kernel refs for Sargo Type-C breadcrumbs:

```sh
git grep -n -E 'ext_5v_boost|usb2_ext_5v_boost|goog,src-pdo|goog,snk-pdo|goog,port-type|goog,default-role|qcom,no-vbus-vote-with-type-C|pm660_pdphy|cc_sbu_ovp|ext-vbus-supply' aosp-msm/android-msm-bonito-4.9-pie-b4s4 aosp-msm/android-msm-bonito-4.9-android12L lineage-google-msm49/lineage-22.2 pixelexp-google-msm49/thirteen -- arch/arm64/boot/dts/google arch/arm64/boot/dts/qcom
```

Search downstream PD engine behavior:

```sh
git grep -n -E 'ext-vbus|goog,src-pdo|goog,snk-pdo|goog,port-type|goog,default-role|set_vbus|update_vbus_locked|external_vbus|POWER_SUPPLY_PROP_TYPEC_POWER_ROLE' aosp-msm/android-msm-bonito-4.9-android12L -- drivers/usb/pd drivers/power/supply/qcom
```

Search Android userspace Type-C behavior:

```sh
git grep -n -E '/sys/class/typec|power_role|data_role|port_type|supports_usb_power_delivery|usb_data_enabled|a600000.dwc3' aosp-device-bonito/android12L-release lineage-device-bonito/lineage-22.2 -- .
```
