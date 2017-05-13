/*
 * (C) 2013-2017 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

/* website url */
#define WEBSITE	"https://github.com/eworm-de/pacredir#pacredir"

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
#define PACSERVE	"_pacserve_" ID "._tcp"
#define PACDBSERVE	"_pacdbserve_" ID "_" ARCH "._tcp"

/* path to the config file */
#define CONFIGFILE	"/etc/pacredir.conf"
/* these characters are used as delimiter in config file */
#define DELIMITER	" ,;"

/* this is where pacman stores its local copy of db files */
#define SYNCPATH	"/var/lib/pacman/sync"

/* This defines the initial time in seconds after which a host is queried
 * again after a bad request. Time is doubled after every subsequent
 * request. */
#define BADTIME	30

#endif /* _CONFIG_H */

// vim: set syntax=c:
