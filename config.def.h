/*
 * (C) 2013 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

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

/* This is used for default documents. Usually you will not see this anyway. */
#define PAGE307 "<html><head><title>307 temporary redirect</title>" \
		"</head><body>307 temporary redirect: " \
		"<a href=\"%s\">%s</a></body></html>"
#define PAGE404 "<html><head><title>404 Not Found</title>" \
		"</head><body>404 Not Found: %s</body></html>"

/* the ports pacredir, pacserve and pacdbserve listen to */
#define PORT_PACREDIR	7077
#define PORT_PACSERVE	7078
#define PORT_PACDBSERVE	7079

/* avahi service names */
#define PACSERVE	"_pacserve._tcp"
#define PACDBSERVE	"_pacdbserve_" ARCH "._tcp"

/* this is where pacman stores its local copy of db files */
#define SYNCPATH	"/var/lib/pacman/sync"

/* This defines when a host is queried again after a bad request
 * default is 600 seconds (10 minutes) */
#define BADTIME	60 * 10

#endif /* _CONFIG_H */

// vim: set syntax=c:
