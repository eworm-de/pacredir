/*
 * (C) 2013-2014 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef _ARCH_H
#define _ARCH_H

/* the binary needs to know its own architecture */
#if defined __x86_64__
#	define ARCH	"x86_64"
#elif defined __i386__
#	define ARCH	"i686"
#elif defined __ARM_ARCH_7__
#	define ARCH	"armv7h"
#elif defined __ARM_ARCH_6__
#	if defined __VFP_FP__
#		define ARCH	"armv6h"
#	else
#		define ARCH	"arm"
#	endif
#else
#	error Unknown architecture!
#endif

#endif /* _ARCH_H */

// vim: set syntax=c:
