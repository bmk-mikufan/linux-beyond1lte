/*
 * s10e-sleepd: when the Galaxy S10e sleeps.
 *
 * A phone sleeps whenever its screen is off and nothing needs it, and wakes
 * for a call, an SMS, the power button, a double tap or the charger.
 * Mobian's own policy is a laptop's: gsd-power suspends after 15 idle
 * minutes through logind, and everything then takes the resume for a lid
 * opening (Phosh turns the screen on).  So this daemon suspends the kernel
 * itself, as Android does, and logind's suspend targets stay masked: none
 * of userspace hears about a sleep, and a call that wakes the phone rings
 * on a screen that only the call turns on.
 *
 * Asleep is s2idle ("freeze").  It stays awake while
 *  - the screen is on: org.gnome.ScreenSaver of the active session (with
 *    the AOD the panel stays lit, but the screen saver is active);
 *  - a logind block inhibitor covers sleep: gsm2sipd holds one in gateway
 *    mode when the Wi-Fi cannot wake the phone (with brcmfmac's host-wake
 *    line it can: a unicast frame wakes it, as IRQ brcmf_host_wake);
 *  - a session inhibitor covers suspend (gnome-session, GtkApplication);
 *  - ModemManager has a call that has not ended;
 *  - an ALSA PCM runs (music, a ringtone, call audio);
 *  - a kernel wakeup source is active: reading wakeup_count waits for it.
 *    cpd holds cpd-sms for 10 s per SMS and cpd-call through a call, the
 *    modem driver 1 s per CP message;
 *  - for wake_hold seconds after anything but its own clock alarm woke it,
 *    wifi_hold after a packet did (handing one to its socket takes a moment;
 *    a call or SMS it starts holds the phone by itself);
 *  - for the first boot_grace seconds after boot (the CP's one-off file
 *    requests about a minute in).
 *
 * The panel keeps its last frame while asleep, so with the AOD showing an
 * RTC alarm wakes the phone at every minute for aod_hold seconds, for Phosh
 * to draw the new time.  Unless the panel ticks a clock of its own (Phosh
 * starts the S6E3FA7's self_clock with the AOD): then it wakes once an hour,
 * for the date and the battery.
 *
 * Settings: /etc/s10e-sleepd.conf, group [sleep]; s10e.nosleep on the kernel
 * command line, or enabled=false, keeps it awake.  SIGUSR1 logs what holds it.
 * A test run, `s10e-sleepd -v --for SECONDS`, sleeps only until then (wall
 * clock, an RTC alarm wakes it for the end) and stays awake after: a phone
 * that sleeps drops Wi-Fi and USB, and with them the ssh that started it.
 */
#define _GNU_SOURCE
#define G_LOG_DOMAIN "sleepd"
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define CONF "/etc/s10e-sleepd.conf"
#define MM_CALL_STATE_TERMINATED 7
#define GSM_INHIBITOR_FLAG_SUSPEND 4

static struct {
	/* settings, in seconds */
	gboolean enabled, aod;
	guint delay, wake_hold, wifi_hold, aod_hold, boot_grace, poll;

	GMainLoop *loop;
	GDBusConnection *sys;
	/* the active session's bus, connected as its user */
	GDBusConnection *sess;
	uid_t sess_uid;
	guint ss_watch, ss_sub;
	gboolean screen_off;	/* its screen saver is active */

	guint timer;		/* the next try */
	gint64 hold_until;	/* monotonic µs */
	gboolean busy;		/* a step in a thread */
	guint gen;		/* bumped when the screen comes on: a step in flight is void */
	int aod_fd;		/* CLOCK_REALTIME_ALARM timerfd */
	time_t until;		/* a test run's end (realtime), or 0 */
	char *held;		/* what kept it awake last, logged when it changes */
	/* since the last wake that was not the clock alarm */
	guint ticks;
	double slept;
} d = { .aod_fd = -1 };

static void try_sleep (void);

static void
schedule (guint ms)
{
	g_clear_handle_id (&d.timer, g_source_remove);
	d.timer = g_timeout_add_once (ms, (GSourceOnceFunc)try_sleep, NULL);
}

static void
unschedule (void)
{
	g_clear_handle_id (&d.timer, g_source_remove);
}

/* ---- the screen: the active session's screen saver ---- */

static void
set_screen_off (gboolean off)
{
	if (off == d.screen_off)
		return;
	d.screen_off = off;
	g_debug ("screen %s", off ? "off" : "on");
	if (off) {
		schedule (d.delay * 1000);
	} else {
		d.gen++;
		unschedule ();
	}
}

static void
on_active_changed (GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *signal,
		   GVariant *params, gpointer data)
{
	gboolean active;
	if (g_variant_is_of_type (params, G_VARIANT_TYPE ("(b)"))) {
		g_variant_get (params, "(b)", &active);
		set_screen_off (active);
	}
}

static void
on_get_active (GObject *src, GAsyncResult *res, gpointer data)
{
	g_autoptr (GError) err = NULL;
	g_autoptr (GVariant) v = g_dbus_connection_call_finish (G_DBUS_CONNECTION (src), res, &err);
	gboolean active = FALSE;
	if (v)
		g_variant_get (v, "(b)", &active);
	else
		g_debug ("GetActive: %s", err->message);
	set_screen_off (active);
}

static void
on_ss_appeared (GDBusConnection *c, const char *name, const char *owner, gpointer data)
{
	g_dbus_connection_call (c, "org.gnome.ScreenSaver", "/org/gnome/ScreenSaver", "org.gnome.ScreenSaver", "GetActive",
				NULL, G_VARIANT_TYPE ("(b)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, on_get_active, NULL);
}

static void
on_ss_vanished (GDBusConnection *c, const char *name, gpointer data)
{
	/* no shell: nobody knows the screen is off */
	set_screen_off (FALSE);
}

static void
session_drop (void)
{
	if (!d.sess)
		return;
	g_clear_handle_id (&d.ss_watch, g_bus_unwatch_name);
	if (d.ss_sub)
		g_dbus_connection_signal_unsubscribe (d.sess, d.ss_sub);
	d.ss_sub = 0;
	g_clear_object (&d.sess);
	set_screen_off (FALSE);
}

static void
on_sess_closed (GDBusConnection *c, gboolean remote, GError *err, gpointer data)
{
	g_message ("session bus of uid %u closed", d.sess_uid);
	session_drop ();
}

/* the active seat0 session's user, or -1 */
static uid_t
active_uid (void)
{
	g_autoptr (GVariant) v = g_dbus_connection_call_sync (
		d.sys, "org.freedesktop.login1", "/org/freedesktop/login1/seat/seat0", "org.freedesktop.DBus.Properties",
		"Get", g_variant_new ("(ss)", "org.freedesktop.login1.Seat", "ActiveSession"), G_VARIANT_TYPE ("(v)"),
		G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	if (!v)
		return (uid_t)-1;
	g_autoptr (GVariant) sv = NULL;
	g_variant_get (v, "(v)", &sv);
	const char *path = NULL;
	if (!g_variant_is_of_type (sv, G_VARIANT_TYPE ("(so)")))
		return (uid_t)-1;
	g_variant_get (sv, "(&s&o)", NULL, &path);
	if (!path || !strcmp (path, "/"))
		return (uid_t)-1;
	g_autoptr (GVariant) u = g_dbus_connection_call_sync (
		d.sys, "org.freedesktop.login1", path, "org.freedesktop.DBus.Properties", "Get",
		g_variant_new ("(ss)", "org.freedesktop.login1.Session", "User"), G_VARIANT_TYPE ("(v)"),
		G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	if (!u)
		return (uid_t)-1;
	g_autoptr (GVariant) uv = NULL;
	g_variant_get (u, "(v)", &uv);
	guint32 uid;
	if (!g_variant_is_of_type (uv, G_VARIANT_TYPE ("(uo)")))
		return (uid_t)-1;
	g_variant_get (uv, "(u&o)", &uid, NULL);
	return uid;
}

/* A user's bus takes only that user: connect with it as the effective uid
 * (the peer credentials are the connect()'s, EXTERNAL sends geteuid()). */
static GDBusConnection *
connect_as (uid_t uid, GError **err)
{
	g_autofree char *addr = g_strdup_printf ("unix:path=/run/user/%u/bus", uid);
	if (setresuid ((uid_t)-1, uid, (uid_t)-1) < 0) {
		g_set_error (err, G_IO_ERROR, g_io_error_from_errno (errno), "seteuid %u: %s", uid, g_strerror (errno));
		return NULL;
	}
	GDBusConnection *c = g_dbus_connection_new_for_address_sync (
		addr, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL,
		NULL, err);
	if (setresuid ((uid_t)-1, 0, (uid_t)-1) < 0)
		g_error ("cannot become root again: %s", g_strerror (errno));
	return c;
}

static gboolean
session_check (gpointer data)
{
	uid_t uid = active_uid ();
	if (d.sess && uid == d.sess_uid)
		return G_SOURCE_CONTINUE;
	session_drop ();
	if (uid == (uid_t)-1)
		return G_SOURCE_CONTINUE;
	g_autoptr (GError) err = NULL;
	d.sess = connect_as (uid, &err);
	if (!d.sess) {
		g_debug ("session bus of uid %u: %s", uid, err->message);
		return G_SOURCE_CONTINUE;
	}
	d.sess_uid = uid;
	g_dbus_connection_set_exit_on_close (d.sess, FALSE);
	g_signal_connect (d.sess, "closed", G_CALLBACK (on_sess_closed), NULL);
	d.ss_sub = g_dbus_connection_signal_subscribe (d.sess, "org.gnome.ScreenSaver", "org.gnome.ScreenSaver",
						       "ActiveChanged", "/org/gnome/ScreenSaver", NULL,
						       G_DBUS_SIGNAL_FLAGS_NONE, on_active_changed, NULL, NULL);
	d.ss_watch = g_bus_watch_name_on_connection (d.sess, "org.gnome.ScreenSaver", G_BUS_NAME_WATCHER_FLAGS_NONE,
						     on_ss_appeared, on_ss_vanished, NULL, NULL);
	g_message ("following the screen of uid %u", uid);
	return G_SOURCE_CONTINUE;
}

/* ---- what holds it awake ---- */

static char *
logind_blocker (void)
{
	g_autoptr (GVariant) v = g_dbus_connection_call_sync (d.sys, "org.freedesktop.login1", "/org/freedesktop/login1",
							      "org.freedesktop.login1.Manager", "ListInhibitors", NULL,
							      G_VARIANT_TYPE ("(a(ssssuu))"), G_DBUS_CALL_FLAGS_NONE,
							      2000, NULL, NULL);
	if (!v)
		return NULL;
	g_autoptr (GVariantIter) it = NULL;
	const char *what, *who, *why, *mode;
	g_variant_get (v, "(a(ssssuu))", &it);
	while (g_variant_iter_next (it, "(&s&s&s&suu)", &what, &who, &why, &mode, NULL, NULL)) {
		g_auto (GStrv) w = g_strsplit (what, ":", -1);
		if (!strcmp (mode, "block") && g_strv_contains ((const char *const *)w, "sleep"))
			return g_strdup_printf ("%s (%s)", who, why);
	}
	return NULL;
}

static char *
session_blocker (void)
{
	if (!d.sess)
		return NULL;
	g_autoptr (GVariant) v = g_dbus_connection_call_sync (
		d.sess, "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager", "IsInhibited",
		g_variant_new ("(u)", GSM_INHIBITOR_FLAG_SUSPEND), G_VARIANT_TYPE ("(b)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL,
		NULL);
	gboolean inhibited = FALSE;
	if (v)
		g_variant_get (v, "(b)", &inhibited);
	return inhibited ? g_strdup ("an app's session inhibitor") : NULL;
}

static GVariant *
get_prop (GDBusConnection *c, const char *name, const char *path, const char *iface, const char *prop)
{
	g_autoptr (GVariant) v = g_dbus_connection_call_sync (c, name, path, "org.freedesktop.DBus.Properties", "Get",
							      g_variant_new ("(ss)", iface, prop), G_VARIANT_TYPE ("(v)"),
							      G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	if (!v)
		return NULL;
	GVariant *inner;
	g_variant_get (v, "(v)", &inner);
	return inner;
}

static char *
call_blocker (void)
{
	g_autoptr (GVariant) v = g_dbus_connection_call_sync (
		d.sys, "org.freedesktop.ModemManager1", "/org/freedesktop/ModemManager1", "org.freedesktop.DBus.ObjectManager",
		"GetManagedObjects", NULL, G_VARIANT_TYPE ("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL,
		NULL);
	if (!v)
		return NULL;
	g_autoptr (GVariantIter) objs = NULL;
	g_variant_get (v, "(a{oa{sa{sv}}})", &objs);
	const char *path;
	GVariant *ifaces;
	while (g_variant_iter_next (objs, "{&o@a{sa{sv}}}", &path, &ifaces)) {
		g_autoptr (GVariant) voice = g_variant_lookup_value (ifaces, "org.freedesktop.ModemManager1.Modem.Voice",
								     G_VARIANT_TYPE ("a{sv}"));
		g_variant_unref (ifaces);
		g_autoptr (GVariant) calls = voice ? g_variant_lookup_value (voice, "Calls", G_VARIANT_TYPE ("ao")) : NULL;
		if (!calls)
			continue;
		for (gsize i = 0; i < g_variant_n_children (calls); i++) {
			const char *call;
			g_variant_get_child (calls, i, "&o", &call);
			g_autoptr (GVariant) st = get_prop (d.sys, "org.freedesktop.ModemManager1", call,
							    "org.freedesktop.ModemManager1.Call", "State");
			if (st && g_variant_is_of_type (st, G_VARIANT_TYPE_INT32) &&
			    g_variant_get_int32 (st) != MM_CALL_STATE_TERMINATED)
				return g_strdup ("a call");
		}
	}
	return NULL;
}

static char *
pcm_blocker (void)
{
	glob_t g;
	char *found = NULL;
	if (glob ("/proc/asound/card*/pcm*/sub*/status", 0, NULL, &g))
		return NULL;
	for (size_t i = 0; i < g.gl_pathc && !found; i++) {
		g_autofree char *s = NULL;
		if (!g_file_get_contents (g.gl_pathv[i], &s, NULL, NULL))
			continue;
		if (strstr (s, "state: RUNNING") || strstr (s, "state: DRAINING")) {
			/* /proc/asound/card0/pcm1p/sub0/status */
			g_auto (GStrv) p = g_strsplit (g.gl_pathv[i], "/", -1);
			found = g_strdup_printf ("audio (%s %s)", p[3], p[4]);
		}
	}
	globfree (&g);
	return found;
}

/* what keeps it awake now, or NULL */
static char *
blocker (void)
{
	char *why;
	if ((why = logind_blocker ()) || (why = session_blocker ()) || (why = call_blocker ()) || (why = pcm_blocker ()))
		return why;
	return NULL;
}

static void
note_held (const char *why)
{
	if (g_strcmp0 (why, d.held)) {
		if (why)
			g_message ("awake: %s", why);
		else if (d.held)
			g_message ("nothing holds it awake now");
		g_free (d.held);
		d.held = g_strdup (why);
	}
}

/* ---- the clock alarm for the AOD ---- */

/* the built-in panel is lit: with the screen saver active, that is the AOD */
static gboolean
aod_showing (void)
{
	glob_t g;
	gboolean on = FALSE;
	if (glob ("/sys/class/drm/card*-DSI-*/dpms", 0, NULL, &g))
		return FALSE;
	for (size_t i = 0; i < g.gl_pathc && !on; i++) {
		g_autofree char *s = NULL;
		on = g_file_get_contents (g.gl_pathv[i], &s, NULL, NULL) && g_str_has_prefix (s, "On");
	}
	globfree (&g);
	return on;
}

/* the panel draws and ticks the clock itself */
static gboolean
panel_clock (void)
{
	glob_t g;
	gboolean on = FALSE;
	if (glob ("/sys/bus/mipi-dsi/devices/*/self_clock", 0, NULL, &g))
		return FALSE;
	for (size_t i = 0; i < g.gl_pathc && !on; i++) {
		g_autofree char *s = NULL;
		on = g_file_get_contents (g.gl_pathv[i], &s, NULL, NULL) && g_str_has_prefix (s, "analog");
	}
	globfree (&g);
	return on;
}

/* at the next minute for the AOD (the next hour with the panel's clock),
 * at a test run's end, or off */
static void
aod_arm (gboolean on, gboolean hourly)
{
	struct itimerspec it = { 0 };
	if (on) {
		struct timespec now;
		time_t step = hourly ? 3600 : 60;
		clock_gettime (CLOCK_REALTIME, &now);
		it.it_value.tv_sec = (now.tv_sec / step + 1) * step;
	}
	if (d.until && (!it.it_value.tv_sec || d.until < it.it_value.tv_sec))
		it.it_value.tv_sec = d.until;
	if (timerfd_settime (d.aod_fd, TFD_TIMER_ABSTIME, &it, NULL) < 0)
		g_warning ("clock alarm: %s", g_strerror (errno));
}

/* whether the alarm went off; it is off after */
static gboolean
aod_fired (void)
{
	guint64 n = 0;
	gboolean fired = read (d.aod_fd, &n, sizeof n) == sizeof n && n > 0;
	struct itimerspec off = { 0 };
	timerfd_settime (d.aod_fd, 0, &off, NULL);
	return fired;
}

/* ---- sleeping: wakeup_count, then the state ---- */

static gboolean
write_file (const char *path, const char *s)
{
	int fd = open (path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return FALSE;
	ssize_t n = write (fd, s, strlen (s));
	int e = errno;
	close (fd);
	errno = e;
	return n == (ssize_t)strlen (s);
}

static double
boottime (void)
{
	struct timespec t;
	clock_gettime (CLOCK_BOOTTIME, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

typedef struct {
	guint gen;
	char count[32];
	int err;	/* of the suspend: 0 = slept */
	double slept;
} Step;

/* Reading wakeup_count waits while any wakeup source is active (a CP
 * message, cpd's SMS and call locks, a key). */
static void
count_thread (GTask *task, gpointer src, gpointer data, GCancellable *c)
{
	Step *s = data;
	int fd = open ("/sys/power/wakeup_count", O_RDONLY | O_CLOEXEC);
	ssize_t n = fd < 0 ? -1 : read (fd, s->count, sizeof s->count - 1);
	int e = errno;
	if (fd >= 0)
		close (fd);
	if (n <= 0) {
		g_task_return_new_error (task, G_IO_ERROR, g_io_error_from_errno (e), "wakeup_count: %s", g_strerror (e));
		return;
	}
	s->count[n] = 0;
	g_strstrip (s->count);
	g_task_return_boolean (task, TRUE);
}

/* Writing the count back fails if a wakeup event came after it was read;
 * a write to the state then fails with EBUSY for one that comes later. */
static void
sleep_thread (GTask *task, gpointer src, gpointer data, GCancellable *c)
{
	Step *s = data;
	if (!write_file ("/sys/power/wakeup_count", s->count)) {
		s->err = errno;
	} else {
		double t0 = boottime ();
		s->err = write_file ("/sys/power/state", "freeze") ? 0 : errno;
		s->slept = boottime () - t0;
	}
	g_task_return_boolean (task, TRUE);
}

static void
hold (guint seconds)
{
	d.hold_until = MAX (d.hold_until, g_get_monotonic_time () + (gint64)seconds * G_USEC_PER_SEC);
}

/* brcmfmac's out-of-band line: a frame for the phone came in on Wi-Fi */
#define WIFI_WAKE_IRQ "brcmf_host_wake"

/* the wake interrupt, as /proc/interrupts names it; *wifi when it was the Wi-Fi */
static char *
wake_irq (gboolean *wifi)
{
	g_autofree char *irq = NULL, *all = NULL;
	*wifi = FALSE;
	if (!g_file_get_contents ("/sys/power/pm_wakeup_irq", &irq, NULL, NULL))
		return g_strdup ("no interrupt");
	g_strstrip (irq);
	if (g_file_get_contents ("/proc/interrupts", &all, NULL, NULL)) {
		g_auto (GStrv) lines = g_strsplit (all, "\n", -1);
		for (char **l = lines; *l; l++) {
			const char *p = *l;
			while (*p == ' ')
				p++;
			if (g_str_has_prefix (p, irq) && p[strlen (irq)] == ':') {
				g_auto (GStrv) f = g_strsplit_set (p, " \t", -1);
				const char *name = NULL;
				for (char **x = f; *x; x++)
					if (**x)
						name = *x;
				*wifi = !g_strcmp0 (name, WIFI_WAKE_IRQ);
				return g_strdup_printf ("IRQ %s (%s)", irq, name ? name : "?");
			}
		}
	}
	return g_strdup_printf ("IRQ %s", irq);
}

static void
on_slept (GObject *src, GAsyncResult *res, gpointer data)
{
	Step *s = g_task_get_task_data (G_TASK (res));
	d.busy = FALSE;
	gboolean tick = aod_fired ();
	if (s->err == EINVAL || s->err == EBUSY) {
		/* a wakeup event in between: whatever it was gets its time */
		g_debug ("sleep cancelled by a wakeup event (%s)", g_strerror (s->err));
		hold (1);
	} else if (s->err) {
		g_warning ("suspend: %s", g_strerror (s->err));
		hold (d.poll);
	} else {
		d.slept += s->slept;
		if (tick && d.screen_off && s->gen == d.gen && (!d.until || time (NULL) < d.until)) {
			/* its own alarm: draw the new minute and sleep on */
			d.ticks++;
			hold (d.aod_hold);
		} else {
			gboolean wifi;
			g_autofree char *why = wake_irq (&wifi);
			if (d.ticks)
				g_message ("slept %.1f s in %u parts (AOD alarm), woken by %s", d.slept, d.ticks + 1, why);
			else
				g_message ("slept %.1f s, woken by %s", d.slept, why);
			d.ticks = 0;
			d.slept = 0;
			hold (wifi ? d.wifi_hold : d.wake_hold);
		}
	}
	if (d.screen_off)
		schedule (0);
}

static void
on_count (GObject *src, GAsyncResult *res, gpointer data)
{
	g_autoptr (GError) err = NULL;
	Step *s = g_task_get_task_data (G_TASK (res));
	d.busy = FALSE;
	if (!g_task_propagate_boolean (G_TASK (res), &err)) {
		g_warning ("%s", err->message);
		schedule (d.poll * 1000);
		return;
	}
	/* the screen came on while a wakeup source held it (and maybe went
	 * off again, whose try found this step busy) */
	if (s->gen != d.gen || !d.screen_off) {
		if (d.screen_off)
			schedule (d.delay * 1000);
		return;
	}
	/* or something else started */
	g_autofree char *why = blocker ();
	note_held (why);
	if (why) {
		schedule (d.poll * 1000);
		return;
	}
	Step *n = g_new0 (Step, 1);
	*n = *s;
	gboolean aod = d.aod && aod_showing ();
	gboolean hourly = aod && panel_clock ();
	aod_arm (aod, hourly);
	g_debug ("sleeping (wakeup_count %s%s)", n->count,
		 hourly ? ", the panel's clock, alarm on the hour" : aod ? ", AOD clock alarm" : "");
	d.busy = TRUE;
	g_autoptr (GTask) t = g_task_new (NULL, NULL, on_slept, NULL);
	g_task_set_task_data (t, n, g_free);
	g_task_run_in_thread (t, sleep_thread);
}

static void
try_sleep (void)
{
	d.timer = 0;
	if (d.until && time (NULL) >= d.until) {
		g_message ("test run over: staying awake");
		d.until = 0;
		d.enabled = FALSE;
	}
	if (!d.enabled || d.busy || !d.screen_off)
		return;
	gint64 now = g_get_monotonic_time ();
	double up = boottime ();
	if (up < d.boot_grace) {
		schedule ((guint)((d.boot_grace - up) * 1000) + 100);
		return;
	}
	if (now < d.hold_until) {
		schedule ((guint)((d.hold_until - now) / 1000) + 1);
		return;
	}
	g_autofree char *why = blocker ();
	note_held (why);
	if (why) {
		/* inhibitors, calls and PCMs say nothing when they end: look again */
		schedule (d.poll * 1000);
		return;
	}
	Step *s = g_new0 (Step, 1);
	s->gen = d.gen;
	d.busy = TRUE;
	g_autoptr (GTask) t = g_task_new (NULL, NULL, on_count, NULL);
	g_task_set_task_data (t, s, g_free);
	g_task_run_in_thread (t, count_thread);
}

/* ---- setup ---- */

static guint
conf_uint (GKeyFile *kf, const char *key, guint def)
{
	g_autoptr (GError) err = NULL;
	int v = g_key_file_get_integer (kf, "sleep", key, &err);
	return err || v < 0 ? def : (guint)v;
}

static gboolean
conf_bool (GKeyFile *kf, const char *key, gboolean def)
{
	g_autoptr (GError) err = NULL;
	gboolean v = g_key_file_get_boolean (kf, "sleep", key, &err);
	return err ? def : v;
}

static void
load_conf (void)
{
	g_autoptr (GKeyFile) kf = g_key_file_new ();
	g_autoptr (GError) err = NULL;
	if (!g_key_file_load_from_file (kf, CONF, G_KEY_FILE_NONE, &err) &&
	    !g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		g_warning ("%s: %s", CONF, err->message);
	d.enabled = conf_bool (kf, "enabled", TRUE);
	d.aod = conf_bool (kf, "aod_clock", TRUE);
	d.delay = conf_uint (kf, "delay", 5);
	d.wake_hold = conf_uint (kf, "wake_hold", 5);
	d.wifi_hold = conf_uint (kf, "wifi_hold", 2);
	d.aod_hold = conf_uint (kf, "aod_hold", 2);
	d.boot_grace = conf_uint (kf, "boot_grace", 120);
	d.poll = MAX (conf_uint (kf, "poll", 10), 1);
	g_autofree char *cmdline = NULL;
	if (g_file_get_contents ("/proc/cmdline", &cmdline, NULL, NULL)) {
		g_auto (GStrv) args = g_strsplit_set (g_strstrip (cmdline), " ", -1);
		if (g_strv_contains ((const char *const *)args, "s10e.nosleep"))
			d.enabled = FALSE;
	}
}

static gboolean
on_quit (gpointer data)
{
	g_main_loop_quit (d.loop);
	return G_SOURCE_REMOVE;
}

static gboolean
on_usr1 (gpointer data)
{
	g_autofree char *why = blocker ();
	gint64 left = d.hold_until - g_get_monotonic_time ();
	g_autofree char *hold = left > 0 ? g_strdup_printf (", held after a wake for %.1f s more", left / 1e6) : NULL;
	g_message ("screen %s, %s%s%s", d.screen_off ? "off" : d.sess ? "on" : "unknown (no session)",
		   why ? "held by " : "nothing holds it", why ? why : "", hold ? hold : "");
	return G_SOURCE_CONTINUE;
}

int
main (int argc, char **argv)
{
	gboolean verbose = FALSE;
	int test_s = 0;
	GOptionEntry entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Log every sleep", NULL },
		{ "for", 0, 0, G_OPTION_ARG_INT, &test_s, "Test run: sleep only for the next SECONDS, then stay awake",
		  "SECONDS" },
		{ NULL, 0, 0, 0, NULL, NULL, NULL },
	};
	g_autoptr (GOptionContext) oc = g_option_context_new ("- sleep when the screen is off");
	g_option_context_add_main_entries (oc, entries, NULL);
	g_autoptr (GError) oerr = NULL;
	if (!g_option_context_parse (oc, &argc, &argv, &oerr)) {
		g_printerr ("%s\n", oerr->message);
		return 2;
	}
	if (verbose)
		g_log_set_debug_enabled (TRUE);
	load_conf ();
	if (test_s > 0) {
		d.until = time (NULL) + test_s;
		d.enabled = TRUE;
		d.boot_grace = 0;
	}
	g_autoptr (GError) err = NULL;
	d.sys = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!d.sys) {
		g_printerr ("system bus: %s\n", err->message);
		return 1;
	}
	d.aod_fd = timerfd_create (CLOCK_REALTIME_ALARM, TFD_NONBLOCK | TFD_CLOEXEC);
	if (d.aod_fd < 0) {
		g_printerr ("CLOCK_REALTIME_ALARM: %s\n", g_strerror (errno));
		return 1;
	}
	d.loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add (SIGTERM, on_quit, NULL);
	g_unix_signal_add (SIGINT, on_quit, NULL);
	g_unix_signal_add (SIGUSR1, on_usr1, NULL);
	g_message ("%s: sleep %u s after the screen goes off, %u s awake after a wake (%u s after Wi-Fi)%s%s",
		   d.enabled ? "on" : "OFF", d.delay, d.wake_hold, d.wifi_hold,
		   d.aod ? ", AOD clock alarm every minute (hour with the panel's clock)" : "",
		   d.until ? ", test run" : "");
	session_check (NULL);
	/* logind tells of a new session too, but a look every few seconds is
	 * cheaper than following seat0 */
	g_timeout_add_seconds (d.poll, session_check, NULL);
	g_main_loop_run (d.loop);
	session_drop ();
	return 0;
}
