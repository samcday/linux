
## 7. Human touch tests (pem120 answers via desktop dialog, `replies/`)
| test | state | pem120 | result |
|------|-------|--------|--------|
| t1 | guarded rev2 TWRP, after E08 vendor-probe replay (T6 diag 0x80/0x81 + REPORTALL only), read-only IRQ monitor | "Done - screen OFF" (uptime 6205.6→6241.1) | **0 CHG transitions** (`t1-e08irq2h-5263.jsonl`) |
| t2 | **rev3** (stock driver in-kernel, config/fw names absent → no upload/BACKUPNV/reset) | "Done - screen OFF" (164.8→196.3) | **WORKS**: 4316 evdev events, 10 touch-downs, X 5–983 / Y 12–1894, smooth drags (`rev3/rev3-ev-t2.jsonl` 2ceb9996…), IRQ 13 count 986 |
| t3 | rev3 after one fb blank→unblank (vendor suspend/resume) | "Done - tapped all 3 keys" (431.6→454.8) | **WORKS** after the cycle (IRQ 1441, 455 T100 msgs). **No T97 key messages at all** → capacitive keys dead on this replacement panel |

rev3 probe log (`rev3/dmesg-boot.txt`): MIUI path, `Failure to request config file …E08-absent-cfg.fw`
(60 s user-helper timeout), input4 registered at 68.3 s; first IRQ at 70.53 s right after
TWRP started (its startup blank/unblank ran the vendor suspend/resume below).

### Vendor suspend/resume writes (debug_enable=1, `rev3/blank-cycle-1.txt`)
blank  : T7 0x38e=0 0x38f=0 0x390=0 · T97 CTRL 0x614=0 0x61e=0 0x628=0 · T19 0x3a7=0xff
unblank: T97 CTRL ×3 = 3 · T19 0x3a7=0 · T7 0x390=0x19 0x38f=0x09 0x38e=0x20 · +100 ms T6 CALIBRATE 0x190=1 (poll to 0)
→ IRQ with a well-formed `01 10 ff b0 76` (T6 CAL, live CRC 0x76B0FF, not the canned 0x55571E).

**Conclusion:** chip + NVM are fine; no config upload/BACKUPNV is needed. The stock driver's
runtime sequence starts the controller reporting; the guarded replay (no T7/T97/T19/CAL writes)
and mainline (quirked writes; T7 rewritten with unchanged values) never did.
Leading hypothesis: the T7 deep-sleep(0) → run transition (+ calibrate) is the trigger.
