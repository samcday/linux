#!/sbin/sh
# read working-state regs via the vendor driver's mem_access, then one blank/unblank cycle with debug on
M=/sys/bus/i2c/devices/5-004a/mem_access
rd() { echo -n "$1 @$2 x$3: "; dd if=$M bs=1 skip=$2 count=$3 2>/dev/null | od -An -tx1 | tr -d '\n'; echo; }
cat /proc/uptime
rd T44 386 1
rd T7 910 5
rd info 0 7
rd T38 478 8
echo 1 > /sys/bus/i2c/devices/5-004a/debug_enable
dmesg -c > /dev/null
echo "irq before: $(grep atmel /proc/interrupts | awk '{print $2}')"
echo 4 > /sys/class/graphics/fb0/blank; echo "blank rc=$?"; sleep 2
echo "irq after blank: $(grep atmel /proc/interrupts | awk '{print $2}')"
echo 0 > /sys/class/graphics/fb0/blank; echo "unblank rc=$?"; sleep 2
echo "irq after unblank: $(grep atmel /proc/interrupts | awk '{print $2}')"
rd T7 910 5
dmesg | grep -i -E "atmel|mxt" | head -80
