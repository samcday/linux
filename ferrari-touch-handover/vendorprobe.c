/*
 * E08 vendor-probe replay for the ferrari maXTouch (clone) controller.
 *
 * Mode "init": replays exactly the I2C transactions that the stock Xiaomi
 * atmel_mxt_ts_336t driver issues on its "Config CRC OK" probe path
 * (as seen in the MIUI dmesg), using the vendor framing:
 *   read : [lo hi] + repeated-start read
 *   write: [lo hi val]  (plain, no dummy byte / no address-1 quirk)
 * The only writes are three volatile T6 commands:
 *   T6.DIAGNOSTIC(0x0193)=0x80, T6.DIAGNOSTIC=0x81, T6.REPORTALL(0x0191)=0x01
 * No RESET, BACKUPNV, CALIBRATE, config or firmware writes.  If T7 reads
 * back zero (the vendor would soft-reset) we stop instead.
 *
 * Mode "irq SECONDS": read-only. Polls CHG (gpio915) every 1 ms and, while
 * CHG is low, performs the vendor mxt_read_messages_t44() reads:
 *   read(0x0182, 11); count = buf[0]; read(0x0183, (count-1)*10)
 * Rate-limited to one handler run per 5 ms; payloads logged when changed.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define SLAVE 0x4a
#define T44 0x0182
#define T5 0x0183
#define T5_MSG 10
#define MAX_REPORTID 60
#define T6_REPORTALL 0x0191
#define T6_DIAG 0x0193
#define T37 0x0100
#define T38 0x01de
#define T7 0x038e

static int i2c_fd = -1, gpio_fd = -1;
static FILE *out;
static int failed;

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_BOOTTIME, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int chg(void)
{
	char b[4] = {0};
	if (lseek(gpio_fd, 0, SEEK_SET) < 0 || read(gpio_fd, b, 1) != 1)
		return -1;
	return b[0] == '1' ? 1 : b[0] == '0' ? 0 : -1;
}

static void hex(const uint8_t *d, int n)
{
	for (int i = 0; i < n; i++)
		fprintf(out, "%02x", d[i]);
}

static int rd(const char *what, uint16_t reg, uint16_t len, uint8_t *buf, int log)
{
	uint8_t p[2] = {reg & 0xff, reg >> 8};
	struct i2c_msg m[2] = {
		{.addr = SLAVE, .flags = 0, .len = 2, .buf = p},
		{.addr = SLAVE, .flags = I2C_M_RD, .len = len, .buf = buf},
	};
	struct i2c_rdwr_ioctl_data x = {.msgs = m, .nmsgs = 2};
	int c0 = chg();
	double t = now_s();
	memset(buf, 0xa5, len);
	errno = 0;
	int ret = ioctl(i2c_fd, I2C_RDWR, &x);
	int e = errno;
	int c1 = chg();
	if (log || ret != 2) {
		fprintf(out, "{\"t\":%.6f,\"op\":\"rd\",\"what\":\"%s\",\"reg\":\"%04x\",\"len\":%u,"
			"\"ret\":%d,\"errno\":%d,\"chg0\":%d,\"chg1\":%d,\"hex\":\"",
			t, what, reg, len, ret, e, c0, c1);
		if (ret == 2)
			hex(buf, len);
		fprintf(out, "\"}\n");
		fflush(out);
	}
	if (ret != 2) {
		failed = 1;
		return -1;
	}
	return 0;
}

static int wr(const char *what, uint16_t reg, uint8_t val)
{
	uint8_t b[3] = {reg & 0xff, reg >> 8, val};
	struct i2c_msg m = {.addr = SLAVE, .flags = 0, .len = 3, .buf = b};
	struct i2c_rdwr_ioctl_data x = {.msgs = &m, .nmsgs = 1};
	int c0 = chg();
	double t = now_s();
	errno = 0;
	int ret = ioctl(i2c_fd, I2C_RDWR, &x);
	int e = errno;
	int c1 = chg();
	fprintf(out, "{\"t\":%.6f,\"op\":\"wr\",\"what\":\"%s\",\"reg\":\"%04x\",\"val\":\"%02x\","
		"\"ret\":%d,\"errno\":%d,\"chg0\":%d,\"chg1\":%d}\n",
		t, what, reg, val, ret, e, c0, c1);
	fflush(out);
	if (ret != 1) {
		failed = 1;
		return -1;
	}
	return 0;
}

static void note(const char *s)
{
	fprintf(out, "{\"t\":%.6f,\"op\":\"note\",\"chg\":%d,\"msg\":\"%s\"}\n", now_s(), chg(), s);
	fflush(out);
}

/* vendor: write T6.DIAG, poll T6.DIAG until 0 (100 x 10 ms) */
static int diag(uint8_t cmd)
{
	uint8_t v;
	int i;
	if (wr("T6.DIAG", T6_DIAG, cmd))
		return -1;
	for (i = 0; i < 100; i++) {
		if (rd("T6.DIAG.poll", T6_DIAG, 1, &v, i < 3 || v == 0))
			return -1;
		if (v == 0)
			break;
		usleep(10000);
	}
	fprintf(out, "{\"t\":%.6f,\"op\":\"poll\",\"cmd\":\"%02x\",\"iterations\":%d,\"last\":\"%02x\"}\n",
		now_s(), cmd, i, v);
	fflush(out);
	return 0;
}

static int do_init(void)
{
	uint8_t b[700], actv, idle, a2i;
	int tries, n;

	note("init: vendor mxt_initialize replay start");
	if (rd("info", 0x0000, 7, b, 1) || rd("objtable", 0x0007, 246, b, 1) ||
	    rd("T38.flag", T38, 1, b, 1) || rd("infocrc", 0x00fd, 3, b, 1))
		return -1;
	if (rd("T7.ACTV", T7 + 1, 1, &actv, 1) || rd("T7.IDLE", T7, 1, &idle, 1) ||
	    rd("T7.A2I", T7 + 2, 1, &a2i, 1))
		return -1;
	if (actv == 0 || idle == 0) {
		note("T7 zero: vendor would soft-reset here; stopping (not authorised in this replay)");
		return -1;
	}

	/* mxt_check_reg_init */
	if (diag(0x80) || rd("T37.revid", T37 + 21, 1, b, 1))
		return -1;
	if (rd("T38.cfginfo", T38, 8, b, 1))
		return -1;
	if (diag(0x81) || rd("T37.lockdown", T37 + 4, 8, b, 1))
		return -1;

	/* mxt_download_config -> mxt_read_current_crc */
	if (wr("T6.REPORTALL", T6_REPORTALL, 1))
		return -1;
	usleep(30000);
	for (tries = 2; tries; tries--) {
		if (rd("T5.drain", T5, T5_MSG * MAX_REPORTID, b, 1))
			return -1;
		for (n = 0; n < MAX_REPORTID; n++) {
			uint8_t id = b[n * T5_MSG];
			if (id == 0 || id > MAX_REPORTID)
				break;
		}
		fprintf(out, "{\"t\":%.6f,\"op\":\"drain\",\"valid\":%d}\n", now_s(), n);
		fflush(out);
		if (n < MAX_REPORTID)
			break;
	}

	/* post-state snapshot (reads only) */
	usleep(50000);
	rd("post.T44", T44, 1, b, 1);
	rd("post.T44T5", T44, 11, b, 1);
	rd("post.T5x1", T5, T5_MSG, b, 1);
	rd("post.T5x61", T5, 610, b, 1);
	note("init: done");
	return 0;
}

static int do_irq(int seconds)
{
	uint8_t b[700], last[700];
	int last_len = -1, last_chg = chg();
	long runs = 0, logged = 0, trans = 0;
	double t0 = now_s(), end = t0 + seconds, last_run = 0, last_beat = t0;

	note("irq: vendor mxt_read_messages_t44 emulation (read-only) start");
	while (now_s() < end && !failed) {
		int c = chg();
		double t = now_s();
		if (c < 0) {
			note("gpio read error");
			failed = 1;
			break;
		}
		if (c != last_chg) {
			trans++;
			fprintf(out, "{\"t\":%.6f,\"op\":\"chg\",\"chg\":%d}\n", t, c);
			fflush(out);
			last_chg = c;
		}
		if (c == 0 && t - last_run >= 0.005) {
			int count, len;
			last_run = t;
			runs++;
			if (rd("irq.T44T5", T44, 11, b, 0))
				break;
			count = b[0];
			len = 11;
			if (count > MAX_REPORTID)
				count = MAX_REPORTID;
			if (count > 1) {
				if (rd("irq.T5rest", T5, (count - 1) * T5_MSG, b + 11, 0))
					break;
				len += (count - 1) * T5_MSG;
			}
			if (len != last_len || memcmp(b, last, len)) {
				logged++;
				fprintf(out, "{\"t\":%.6f,\"op\":\"irq\",\"run\":%ld,\"chg_after\":%d,\"len\":%d,\"hex\":\"",
					t, runs, chg(), len);
				hex(b, len);
				fprintf(out, "\"}\n");
				fflush(out);
				memcpy(last, b, len);
				last_len = len;
			}
		}
		if (t - last_beat >= 10) {
			last_beat = t;
			fprintf(out, "{\"t\":%.6f,\"op\":\"beat\",\"chg\":%d,\"runs\":%ld,\"logged\":%ld,\"transitions\":%ld}\n",
				t, c, runs, logged, trans);
			fflush(out);
		}
		usleep(1000);
	}
	fprintf(out, "{\"t\":%.6f,\"op\":\"end\",\"runs\":%ld,\"logged\":%ld,\"transitions\":%ld,\"failed\":%d,\"chg\":%d}\n",
		now_s(), runs, logged, trans, failed, chg());
	fflush(out);
	return failed ? -1 : 0;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s /dev/i2c-N outfile init | irq SECONDS\n", argv[0]);
		return 2;
	}
	i2c_fd = open(argv[1], O_RDWR | O_CLOEXEC);
	gpio_fd = open("/sys/class/gpio/gpio915/value", O_RDONLY | O_CLOEXEC);
	out = fopen(argv[2], "a");
	if (i2c_fd < 0 || gpio_fd < 0 || !out) {
		perror("open");
		return 1;
	}
	fprintf(out, "{\"t\":%.6f,\"op\":\"start\",\"mode\":\"%s\",\"pid\":%d}\n", now_s(), argv[3], getpid());
	fflush(out);
	if (!strcmp(argv[3], "init"))
		return do_init() ? 1 : 0;
	if (!strcmp(argv[3], "irq") && argc == 5)
		return do_irq(atoi(argv[4])) ? 1 : 0;
	fprintf(stderr, "bad mode\n");
	return 2;
}
