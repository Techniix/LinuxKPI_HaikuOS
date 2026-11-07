/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Runs LinuxKPI's SYSINIT/SYSUNINIT entries in FreeBSD order (subsystem,
 * then order) — see include/sys/kernel.h for why Haiku's own sysinit runner
 * cannot be used. We register ONE entry in Haiku's freebsd_network sysinit
 * set, so everything happens inside _fbsd_init_drivers(): after Haiku's
 * mutexes, mbufs, callouts and taskqueues are up, and before devices attach.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/linker_set.h>
#include <sys/sleepqueue.h>

void lkpi_dpcpu_init(void);	/* lkpi_misc.c */
void lkpi_jiffies_init(void);	/* lkpi_thread.cpp */
void lkpi_jiffies_uninit(void);
void lkpi_control_init(void);	/* lkpi_control.c */
void lkpi_control_uninit(void);

#include <sys/sysctl.h>

#include <KernelExport.h>
#include <driver_settings.h>
#include <stdlib.h>
#include <string.h>

extern const char *gDriverName;	/* freebsd_network compat/sys/haiku-module.h */

/* Keep both sets non-empty so they always link. */
static void lkpi_sysinit_nop(void *arg) { }
static struct lkpi_sysinit lkpi_sysinit_dummy = { "dummy", 0, 0,
	lkpi_sysinit_nop, NULL };
static struct lkpi_sysinit lkpi_sysuninit_dummy = { "dummy", 0, 0,
	lkpi_sysinit_nop, NULL };
DATA_SET(__lkpi_sysinit, lkpi_sysinit_dummy);
DATA_SET(__lkpi_sysuninit, lkpi_sysuninit_dummy);

SET_DECLARE(__lkpi_sysinit, struct lkpi_sysinit);
SET_DECLARE(__lkpi_sysuninit, struct lkpi_sysinit);

/*
 * Tunables (see include/sys/sysctl.h). FreeBSD name from the SYSCTL parent
 * token: "_compat_linuxkpi_80211" + "hw_crypto" -> "compat.linuxkpi.80211.
 * hw_crypto". Node names containing '_' cannot be told apart from the
 * separators; none of the LinuxKPI tunables have one.
 */
static struct lkpi_tunable lkpi_tunable_dummy = { "", "", NULL, 0, 0, 0 };
DATA_SET(__lkpi_tunable, lkpi_tunable_dummy);
SET_DECLARE(__lkpi_tunable, struct lkpi_tunable);

static void
lkpi_tunable_key(const struct lkpi_tunable *t, char *buf, size_t len)
{
	const char *p = t->parent;
	size_t i = 0;

	if (*p == '_')
		p++;
	for (; *p != '\0' && i + 1 < len; p++)
		buf[i++] = (*p == '_') ? '.' : *p;
	if (i + 1 < len)
		buf[i++] = '.';
	buf[i] = '\0';
	strlcat(buf, t->name, len);
}

static void
lkpi_tunables_fetch(void)
{
	struct lkpi_tunable **tp;
	void *handle;
	char key[128];

	handle = load_driver_settings(gDriverName);
	if (handle == NULL)
		return;
	for (tp = SET_BEGIN(__lkpi_tunable); tp < SET_LIMIT(__lkpi_tunable);
	    tp++) {
		struct lkpi_tunable *t = *tp;
		const char *v;
		long n;
		char *end;

		if (t->ptr == NULL || (t->access & CTLFLAG_TUN) == 0)
			continue;
		lkpi_tunable_key(t, key, sizeof(key));
		v = get_driver_parameter(handle, key, NULL, NULL);
		if (v == NULL)
			continue;
		if (strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 ||
		    strcmp(v, "on") == 0)
			n = 1;
		else if (strcmp(v, "false") == 0 || strcmp(v, "no") == 0 ||
		    strcmp(v, "off") == 0)
			n = 0;
		else {
			n = strtol(v, &end, 0);
			if (end == v || *end != '\0') {
				dprintf("linuxkpi: tunable %s: bad value \"%s\"\n",
				    key, v);
				continue;
			}
		}
		switch (t->size) {
		case 1: *(uint8_t *)t->ptr = (uint8_t)n; break;
		case 2: *(uint16_t *)t->ptr = (uint16_t)n; break;
		case 4: *(uint32_t *)t->ptr = (uint32_t)n; break;
		case 8: *(uint64_t *)t->ptr = (uint64_t)n; break;
		default: continue;
		}
		dprintf("linuxkpi: tunable %s = %ld\n", key, n);
	}
	unload_driver_settings(handle);
}

#define LKPI_MAX_SYSINITS	256

static int
lkpi_sysinit_cmp(const struct lkpi_sysinit *a, const struct lkpi_sysinit *b)
{
	if (a->subsystem != b->subsystem)
		return (a->subsystem < b->subsystem ? -1 : 1);
	if (a->order != b->order)
		return (a->order < b->order ? -1 : 1);
	return (0);
}

/* Stable insertion sort: small n, and ties keep link order as FreeBSD's
 * (unspecified but usually link-order) behaviour. */
static int
lkpi_sysinit_collect(struct lkpi_sysinit **begin, struct lkpi_sysinit **end,
    struct lkpi_sysinit **out)
{
	struct lkpi_sysinit **p;
	int n = 0, i;

	for (p = begin; p < end; p++) {
		if ((*p)->func == lkpi_sysinit_nop)
			continue;
		if (n == LKPI_MAX_SYSINITS)
			panic("LinuxKPI: more than %d SYSINITs", LKPI_MAX_SYSINITS);
		for (i = n; i > 0 && lkpi_sysinit_cmp(out[i - 1], *p) > 0; i--)
			out[i] = out[i - 1];
		out[i] = *p;
		n++;
	}
	return (n);
}

static void
lkpi_sysinit_run(void *arg)
{
	static struct lkpi_sysinit *list[LKPI_MAX_SYSINITS];
	int i, n;

	lkpi_sleepq_init();
	lkpi_dpcpu_init();	/* before any SYSINIT touches DPCPU data */
	lkpi_jiffies_init();
	lkpi_tunables_fetch();	/* FreeBSD: tunables before SI_SUB_TUNABLES */
	lkpi_control_init();	/* before devices are published */
	n = lkpi_sysinit_collect(SET_BEGIN(__lkpi_sysinit),
	    SET_LIMIT(__lkpi_sysinit), list);
	for (i = 0; i < n; i++) {
		dprintf("linuxkpi: sysinit %s (%#x/%#x)\n", list[i]->name,
		    list[i]->subsystem, list[i]->order);
		list[i]->func((void *)list[i]->arg);
	}
}

static void
lkpi_sysuninit_run(void *arg)
{
	static struct lkpi_sysinit *list[LKPI_MAX_SYSINITS];
	int i, n;

	/* FreeBSD runs SYSUNINITs in reverse (subsystem, order). */
	lkpi_control_uninit();
	n = lkpi_sysinit_collect(SET_BEGIN(__lkpi_sysuninit),
	    SET_LIMIT(__lkpi_sysuninit), list);
	for (i = n - 1; i >= 0; i--) {
		dprintf("linuxkpi: sysuninit %s\n", list[i]->name);
		list[i]->func((void *)list[i]->arg);
	}
	lkpi_jiffies_uninit();
}

/* The single entry in Haiku's own set (struct sysinit from freebsd_network's
 * compat/sys/kernel.h; its SYSINIT macro is overridden in our overlay). */
static struct sysinit lkpi_haiku_sysinit = {
	.name = "linuxkpi", .order = SI_ORDER_FIRST,
	.func = lkpi_sysinit_run, .arg = NULL,
};
DATA_SET(__freebsd_sysinit, lkpi_haiku_sysinit);

static struct sysinit lkpi_haiku_sysuninit = {
	.name = "linuxkpi", .order = SI_ORDER_FIRST,
	.func = lkpi_sysuninit_run, .arg = NULL,
};
DATA_SET(__freebsd_sysuninit, lkpi_haiku_sysuninit);
