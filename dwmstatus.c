/*
 * Copy me if you can.
 * by 20h
 */

#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>

#define MIXER "/dev/mixer"
#define TZ "America/New_York"
#define TIMEFMT "%F %T"

typedef struct Buf Buf;

struct Buf {
	char *s;
	size_t cap;
};

static Display *dpy;

static int
bprintf(Buf *buf, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf->s, buf->cap, fmt, args);
	va_end(args);

	if ((size_t)len + 1 > buf->cap) {
		buf->cap = (size_t)len + 1;
		if (!(buf->s = realloc(buf->s, buf->cap)))
			err(1, "realloc");
	}

	va_start(args, fmt);
	len = vsnprintf(buf->s, buf->cap, fmt, args);
	va_end(args);

	return len;
}

static int
datetime(Buf *buf, const char *fmt, const char *tzname)
{
	char ts[129];
	time_t tim;
	struct tm *timtm;

	setenv("TZ", tzname, 1);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return bprintf(buf, "");

	if (!strftime(ts, sizeof(ts)-1, fmt, timtm)) {
		warnx("not enough space for time");
		return bprintf(buf, "");
	}

	return bprintf(buf, "%s", ts);
}

static int
findmixerdev(int fd)
{
	static int dev = -1; /* find the device once and keep it here */

	int cls;
	mixer_devinfo_t mdi;

	if (dev >= 0)
		return dev;

	for (mdi.index = 0; ; mdi.index++) {
		if (ioctl(fd, AUDIO_MIXER_DEVINFO, &mdi) < 0) {
			warn("could not find mixer class");
			return -1;
		}

		if (mdi.type == AUDIO_MIXER_CLASS &&
		    !strcmp(mdi.label.name, AudioCoutputs)) {
			cls = mdi.index;
			break;
		}
	}

	for (mdi.index = 0; ; mdi.index++) {
		if (ioctl(fd, AUDIO_MIXER_DEVINFO, &mdi) < 0) {
			warn("no more audio devices");
			return -1;
		}

		if (mdi.mixer_class == cls &&
		    mdi.type == AUDIO_MIXER_VALUE &&
		    !strcmp(mdi.label.name, AudioNmaster))
			return mdi.index;
	}
}

static int
volume(Buf *buf)
{
	int fd, vol;
	mixer_ctrl_t mc;

	if ((fd = open(MIXER, O_RDONLY)) < 0) {
		warn("could not open mixer device");
		return bprintf(buf, "");
	}

	if ((mc.dev = findmixerdev(fd)) < 0)
		goto fail;
	if (ioctl(fd, AUDIO_MIXER_READ, &mc) < 0) {
		warn("could not get volume");
		goto fail;
	}

	vol = 100 * mc.un.value.level[0] / 255;
	close(fd);
	return bprintf(buf, "%d", vol);

fail:
	close(fd);
	return bprintf(buf, "");
}

static void
setstatus(const char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

int
main(void)
{
	Buf status, time, vol;

	memset(&status, 0, sizeof(status));
	memset(&time, 0, sizeof(time));
	memset(&vol, 0, sizeof(vol));

	if (!(dpy = XOpenDisplay(NULL)))
		errx(1, "cannot open display");

	for (;; sleep(1)) {
		datetime(&time, TIMEFMT, TZ);
		volume(&vol);

		bprintf(&status, "VOL: %s%% | %s", vol.s, time.s);
		setstatus(status.s);
	}

	XCloseDisplay(dpy);
	return 0;
}

