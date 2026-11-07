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

#include <KernelExport.h>

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
