#!/usr/bin/env python3
"""Build rev3-TWRP.img: stock TWRP with the Atmel node ENABLED but every
built-in config/firmware name pointed at an absent file, Synaptics disabled.

Effect on the vendor atmel_mxt_ts_336t probe: request_firmware() for the
config fails -> mxt_download_config() returns 0 (no config write, no
BACKUPNV, no reset); request_firmware() for the fw fails before
mxt_load_fw() touches the chip (no bootloader entry).  This is the MIUI
"Config CRC OK" probe path minus the CRC check.

Splice method identical to E06 rev2 (see E06 REPORT.md)."""
import hashlib, re, struct, subprocess, sys

SRC_IMG = '../E06-twrp-readonly/orig-TWRP.img'
SRC_DTS = '../E06-twrp-readonly/orig-TWRP.dts'
FDT_OFF = 27602944
DT_OFF = 27600896
FDT_REGION = 0x39000
ABSENT_CFG = 'xiaomi/ferrari/E08-absent-cfg.fw'
ABSENT_FW = 'xiaomi/ferrari/E08-absent-fw.fw'

img = bytearray(open(SRC_IMG, 'rb').read())
assert hashlib.sha256(img).hexdigest() == \
    '9bd4db24f8703bcf5e9943788291ba4a05601ce04e5eaab8e312de22184ee85f'
dts = open(SRC_DTS).read().split('\n')
assert hashlib.sha256('\n'.join(dts).encode()).hexdigest() == \
    '384bd097fe0ebe0b3be9493ae9350310c6ed982074773118f2321d412119fbe8'

out = []
changes = []
for i, line in enumerate(dts):
    out.append(line)
    if line.strip() == 'synaptics@20 {':
        indent = re.match(r'\s*', dts[i + 1]).group(0)
        out.append(indent + 'status = "disabled";')
        changes.append('synaptics@20 disabled')
    elif 'atmel,mxt-cfg-name = ' in line:
        out[-1] = re.sub(r'"[^"]*"', '"%s"' % ABSENT_CFG, line)
        changes.append('cfg-name line %d' % (i + 1))
    elif 'atmel,mxt-fw-name = ' in line:
        out[-1] = re.sub(r'"[^"]*"', '"%s"' % ABSENT_FW, line)
        changes.append('fw-name line %d' % (i + 1))
assert len(changes) == 6, changes
open('rev3-TWRP.dts', 'w').write('\n'.join(out))
subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', 'rev3-TWRP.dtb',
                'rev3-TWRP.dts'], check=True)
fdt = open('rev3-TWRP.dtb', 'rb').read()
assert fdt[:4] == b'\xd0\x0d\xfe\xed' and len(fdt) <= FDT_REGION

new = bytearray(img)
new[FDT_OFF:FDT_OFF + FDT_REGION] = fdt + b'\0' * (FDT_REGION - len(fdt))
assert len(new) == len(img)

# header fields (Android boot img v0)
ksz, = struct.unpack_from('<I', new, 8)
rsz, = struct.unpack_from('<I', new, 16)
ssz, = struct.unpack_from('<I', new, 24)
page, = struct.unpack_from('<I', new, 36)
dtsz, = struct.unpack_from('<I', new, 40)
assert page == 2048 and ssz == 0 and dtsz == 0x39800
kernel = new[2048:2048 + ksz]
roff = 2048 + ((ksz + page - 1) // page) * page
ramdisk = new[roff:roff + rsz]
dtr = new[DT_OFF:DT_OFF + dtsz]


def digest(k, r, d):
    h = hashlib.sha1()
    h.update(k); h.update(struct.pack('<I', len(k)))
    h.update(r); h.update(struct.pack('<I', len(r)))
    h.update(b''); h.update(struct.pack('<I', 0))
    h.update(d); h.update(struct.pack('<I', len(d)))
    return h.digest()


# formula must reproduce the ORIGINAL stored id first
assert digest(img[2048:2048 + ksz], img[roff:roff + rsz],
              img[DT_OFF:DT_OFF + dtsz]) == bytes(img[576:596]), 'formula'
new[576:596] = digest(kernel, ramdisk, dtr)

# verification: everything outside FDT region + id identical
assert new[:576] == img[:576] and new[596:FDT_OFF] == img[596:FDT_OFF]
assert new[FDT_OFF + FDT_REGION:] == img[FDT_OFF + FDT_REGION:]
open('rev3-TWRP.img', 'wb').write(new)
for f in ('rev3-TWRP.img', 'rev3-TWRP.dtb', 'rev3-TWRP.dts'):
    print(hashlib.sha256(open(f, 'rb').read()).hexdigest(), f)
print('changes:', changes)
