/*
 * evgrab: exclusively grab an evdev node (so TWRP/any UI never sees the
 * touches) and log every event as JSONL for a bounded time.
 * usage: evgrab /dev/input/eventN seconds outfile
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_BOOTTIME, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	char name[128] = "?";
	struct input_event ev[64];
	long n_ev = 0, n_syn = 0, n_abs = 0, n_key = 0;
	double t0, end, beat;
	FILE *out;
	int fd, grab;

	if (argc != 4) {
		fprintf(stderr, "usage: %s /dev/input/eventN seconds outfile\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	out = fopen(argv[3], "a");
	if (fd < 0 || !out) {
		perror("open");
		return 1;
	}
	ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
	grab = ioctl(fd, EVIOCGRAB, 1);
	t0 = now_s();
	end = t0 + atoi(argv[2]);
	beat = t0;
	fprintf(out, "{\"t\":%.6f,\"op\":\"start\",\"dev\":\"%s\",\"name\":\"%s\",\"grab\":%d,\"errno\":%d,\"pid\":%d}\n",
		t0, argv[1], name, grab, grab ? errno : 0, getpid());
	fflush(out);

	while (now_s() < end) {
		struct pollfd p = {.fd = fd, .events = POLLIN};
		int r = poll(&p, 1, 1000);
		double t = now_s();

		if (r > 0) {
			ssize_t len = read(fd, ev, sizeof(ev));
			if (len < 0) {
				fprintf(out, "{\"t\":%.6f,\"op\":\"error\",\"errno\":%d}\n", t, errno);
				break;
			}
			for (size_t i = 0; i < len / sizeof(ev[0]); i++) {
				n_ev++;
				n_syn += ev[i].type == EV_SYN;
				n_abs += ev[i].type == EV_ABS;
				n_key += ev[i].type == EV_KEY;
				fprintf(out, "{\"t\":%.6f,\"ts\":%ld.%06ld,\"type\":%u,\"code\":%u,\"value\":%d}\n",
					t, (long)ev[i].time.tv_sec, (long)ev[i].time.tv_usec,
					ev[i].type, ev[i].code, ev[i].value);
			}
			fflush(out);
		}
		if (t - beat >= 10) {
			beat = t;
			fprintf(out, "{\"t\":%.6f,\"op\":\"beat\",\"events\":%ld,\"syn\":%ld,\"abs\":%ld,\"key\":%ld}\n",
				t, n_ev, n_syn, n_abs, n_key);
			fflush(out);
		}
	}
	fprintf(out, "{\"t\":%.6f,\"op\":\"end\",\"events\":%ld,\"syn\":%ld,\"abs\":%ld,\"key\":%ld}\n",
		now_s(), n_ev, n_syn, n_abs, n_key);
	fclose(out);
	return 0;
}
