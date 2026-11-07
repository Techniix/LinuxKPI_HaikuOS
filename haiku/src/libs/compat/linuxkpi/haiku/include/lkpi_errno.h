/*
 * Copyright 2026, LinuxKPI_HaikuOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Positive, FreeBSD-valued errno constants for LinuxKPI translation units.
 *
 * Haiku's errno values are negative (B_GENERAL_ERROR_BASE == INT_MIN), but
 * Linux and FreeBSD code returns "-EFOO" and tests "err < 0". Inside LinuxKPI
 * and Linux drivers every E* macro therefore takes FreeBSD's value (as on
 * FreeBSD, where LinuxKPI was written). Values crossing into or out of Haiku
 * code (out-of-line freebsd_network functions, Haiku kernel calls) are
 * converted with lkpi_errno_from_haiku() / lkpi_errno_to_haiku()
 * (haiku/lkpi_errno.c). Inline freebsd_network header code compiled inside a
 * LinuxKPI unit sees these values too, which is consistent.
 */
#ifndef _LKPI_HAIKU_ERRNO_H_
#define _LKPI_HAIKU_ERRNO_H_

#include <errno.h>	/* Haiku's, so its guard is set before we override */

#include "lkpi_errno_redefine.h"

#ifndef EOK
#define EOK	0
#endif

/* FreeBSD kernel-internal pseudo errors (sys/errno.h, _KERNEL). */
#ifndef ERESTART
#define ERESTART	(-1)
#endif
#ifndef EJUSTRETURN
#define EJUSTRETURN	(-2)
#endif
#ifndef ENOIOCTL
#define ENOIOCTL	(-3)
#endif
#ifndef EDIRIOCTL
#define EDIRIOCTL	(-4)
#endif
#ifndef ERELOOKUP
#define ERELOOKUP	(-5)
#endif

#ifdef __cplusplus
extern "C" {
#endif
int	lkpi_errno_from_haiku(int haikuError);	/* Haiku status/errno -> FreeBSD errno (>= 0) */
int	lkpi_errno_to_haiku(int fbsdErrno);	/* FreeBSD errno (sign ignored) -> Haiku status_t */
#ifdef __cplusplus
}
#endif

#endif
