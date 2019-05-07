/*
 * Copy me if you can.
 * by 20h
 */

#include <sys/types.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <machine/apmvar.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>

#define TZ "America/New_York"
#define TIMEFMT "%F %T"

#define APM "/dev/apm"
#define MIXER "/dev/mixer"
#define TZDIR "/usr/share/zoneinfo"

typedef struct Buf Buf;

struct Buf {
	char *s;
	size_t cap;
};

static Display *dpy;
static int apmfd, mixerfd;

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
battery(Buf *buf)
{
	struct apm_power_info api;

	if (ioctl(apmfd, APM_IOC_GETPOWER, &api) < 0) {
		warn("could not get battery status");
		return bprintf(buf, "");
	}
	if (api.minutes_left == (unsigned)-1)
		return bprintf(buf, "%u%%", api.battery_life);
	else
		return bprintf(buf, "%u%% (%u:%u)", api.battery_life,
		    api.minutes_left / 60, api.minutes_left % 60);
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
findmixerdev()
{
	static int dev = -1; /* find the device once and keep it here */

	int cls;
	mixer_devinfo_t mdi;

	if (dev >= 0)
		return dev;

	for (mdi.index = 0; ; mdi.index++) {
		if (ioctl(mixerfd, AUDIO_MIXER_DEVINFO, &mdi) < 0) {
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
		if (ioctl(mixerfd, AUDIO_MIXER_DEVINFO, &mdi) < 0) {
			warn("no more audio devices");
			return -1;
		}

		if (mdi.mixer_class == cls &&
		    mdi.type == AUDIO_MIXER_VALUE &&
		    !strcmp(mdi.label.name, AudioNmaster))
			return dev = mdi.index;
	}
}

static int
volume(Buf *buf)
{
	int vol;
	mixer_ctrl_t mc;

	if ((mc.dev = findmixerdev()) < 0)
		return bprintf(buf, "");
	if (ioctl(mixerfd, AUDIO_MIXER_READ, &mc) < 0) {
		warn("could not get volume");
		return bprintf(buf, "");
	}

	vol = 100 * mc.un.value.level[0] / 255;
	return bprintf(buf, "%d", vol);
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
	Buf status, bat, time, vol;

	memset(&status, 0, sizeof(status));
	memset(&bat, 0, sizeof(bat));
	memset(&time, 0, sizeof(time));
	memset(&vol, 0, sizeof(vol));

	if (!(dpy = XOpenDisplay(NULL)))
		errx(1, "cannot open display");

	if (unveil(APM, "r") < 0)
		err(1, "cannot unveil " APM);
	if (unveil(MIXER, "r") < 0)
		err(1, "cannot unveil " MIXER);
	if (unveil(TZDIR, "r") < 0)
		err(1, "cannot unveil " TZDIR);
	if (unveil(NULL, NULL) < 0)
		err(1, "unveil");

	if ((apmfd = open(APM, O_RDONLY)) < 0)
		err(1, "cannot open " APM);
	if ((mixerfd = open(MIXER, O_RDONLY)) < 0)
		err(1, "cannot open " MIXER);

	for (;; sleep(1)) {
		battery(&bat);
		datetime(&time, TIMEFMT, TZ);
		volume(&vol);

		bprintf(&status, "BAT: %s | VOL: %s%% | %s", bat.s, vol.s, time.s);
		setstatus(status.s);
	}

	XCloseDisplay(dpy);
	return 0;
}

