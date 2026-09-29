#!/usr/bin/env python3
"""Check the compiled bring-up DTB, not just its source or schema exit status."""

import argparse
import json
from pathlib import Path
import subprocess


def check(dtb, schema_result=None):
    if schema_result is not None:
        diagnostics = json.loads(schema_result.read_text())
        if diagnostics != []:
            raise ValueError(f"expected empty DT schema diagnostics, got {diagnostics!r}")

    def fdtget(option, *args):
        return subprocess.check_output(
            ["fdtget", option, str(dtb), *args], text=True
        ).strip()

    def text(node, prop):
        return fdtget("-ts", node, prop)

    def cells(node, prop):
        return tuple(int(value, 16) for value in fdtget("-tx", node, prop).split())

    def expect(actual, expected, description):
        if actual != expected:
            raise ValueError(f"{description}: expected {expected!r}, got {actual!r}")

    expect(text("/", "model"), "Sony Xperia 1 VI", "board model")
    expect(text("/", "compatible"), "sony,pdx245 qcom,sm8650", "board compatible")
    expect(text("/chosen", "stdout-path"), "serial0:115200n8", "console")
    uart = text("/aliases", "serial0")
    expect(uart, "/soc@0/geniqup@8c0000/serial@89c000", "UART15 path")
    expect(text(uart, "status"), "okay", "UART15 status")
    expect(text("/soc@0/geniqup@8c0000", "status"), "okay", "UART wrapper")

    usb = "/soc@0/usb@a600000"
    hsphy = "/soc@0/phy@88e3000"
    repeater = "/soc@0/spmi@c400000/pmic@7/phy@fd00"
    rsc = "/soc@0/rsc@17a00000"
    expect(text(usb, "status"), "okay", "USB controller")
    expect(text(usb, "dr_mode"), "peripheral", "USB role")
    expect(text(usb, "maximum-speed"), "high-speed", "USB speed")
    expect(text(usb, "phy-names"), "usb2-phy", "USB PHY selection")
    expect(cells(usb, "phys"), cells(hsphy, "phandle"), "USB2-only PHY")
    expect(text(hsphy, "status"), "okay", "eUSB2 PHY")
    expect(cells(hsphy, "phys"), cells(repeater, "phandle"), "eUSB2 repeater")
    expect(text("/soc@0/phy@88e8000", "status"), "disabled", "USB3/DP PHY")
    usb_properties = set(fdtget("-p", usb).split())
    if "qcom,select-utmi-as-pipe-clk" not in usb_properties:
        raise ValueError("USB2-only operation must not depend on the disabled SSPHY clock")
    if "usb-role-switch" in usb_properties:
        raise ValueError("the diagnostic path must not require USB role switching")
    for node, prop, regulator in [
        (hsphy, "vdd-supply", "regulators-3/ldo1"),
        (hsphy, "vdda12-supply", "regulators-3/ldo3"),
        (repeater, "vdd18-supply", "regulators-0/ldo15"),
        (repeater, "vdd3-supply", "regulators-0/ldo5"),
        ("/soc@0/ufshc@1d84000", "vcc-supply", "regulators-0/ldo17"),
        ("/soc@0/ufshc@1d84000", "vccq-supply", "regulators-1/ldo1"),
        ("/soc@0/phy@1d80000", "vdda-phy-supply", "regulators-2/ldo1"),
        ("/soc@0/phy@1d80000", "vdda-pll-supply", "regulators-3/ldo3"),
    ]:
        expect(cells(node, prop), cells(f"{rsc}/{regulator}", "phandle"), prop)
    expect(text(f"{rsc}/regulators-3", "qcom,pmic-id"), "i", "USB PHY PMIC ID")
    for node in ["/soc@0/ufshc@1d84000", "/soc@0/phy@1d80000"]:
        expect(text(node, "status"), "disabled", "unresolved UFS power sequencing")

    fixed_regions = {
        "smem@81d00000": (0, 0x81D00000, 0, 0x200000),
        "aop-cmd-db@81c60000": (0, 0x81C60000, 0, 0x20000),
        "lost-reg@9b09c000": (0, 0x9B09C000, 0, 0x4000),
        "hwfence-shbuf@d4e23000": (0, 0xD4E23000, 0, 0x2DD000),
    }
    children = fdtget("-l", "/reserved-memory").split()
    if "hwfence-shbuf@e6440000" in children:
        raise ValueError("the reference-board hardware-fence reservation remains")
    for child, reg in fixed_regions.items():
        node = "/reserved-memory/" + child
        expect(cells(node, "reg"), reg, node)
        expect(fdtget("-p", node).split().count("no-map"), 1, f"{node} no-map")

    # A bare inherited reservation is intentionally not assigned to the modem.
    rmtfs = "/reserved-memory/rmtfs@d7c00000"
    properties = set(fdtget("-p", rmtfs).split())
    if properties & {"compatible", "qcom,client-id", "qcom,vmid"}:
        raise ValueError("unverified rmtfs memory must not be assigned to a modem")

    regions = []
    for child in children:
        node = "/reserved-memory/" + child
        reg = cells(node, "reg")
        if len(reg) != 4:
            raise ValueError(f"{node}: expected a single 64-bit fixed reservation")
        start = (reg[0] << 32) | reg[1]
        size = (reg[2] << 32) | reg[3]
        if not size:
            raise ValueError(f"{node}: empty reservation")
        regions.append((start, start + size, node))
    regions.sort()
    for previous, current in zip(regions, regions[1:]):
        if previous[1] > current[0]:
            raise ValueError(f"overlapping reservations: {previous[2]}, {current[2]}")

    print(
        f"{dtb}: PDX245 identity, console, USB/UFS wiring "
        f"and {len(regions)} reservations checked"
    )
    print("Static checks only: no bootloader acceptance or hardware operation is proven.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path)
    parser.add_argument("--schema-result", type=Path, help="require empty dt-validate JSON diagnostics")
    args = parser.parse_args()
    try:
        check(args.dtb, args.schema_result)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"PDX245 DTB check failed: {error}\n")
