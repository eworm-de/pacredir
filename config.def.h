/*
 * (C) 2013-2022 by Christian Hesse <mail@eworm.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H

/* drop privileges by changing uid and gid to these */
#define DROP_PRIV_UID	65534
#define DROP_PRIV_GID	65534

/* website url */
#define WEBSITE	"https://github.com/eworm-de/pacredir#pacredir"

/* This is used for default documents. Usually you will not see this anyway. */
#define PAGE307 "<html><head><title>307 temporary redirect</title>" \
		"</head><body>307 temporary redirect: " \
		"<a href=\"%s\">%s</a></body></html>"
#define PAGE404 "<html><head><title>404 Not Found</title>" \
		"</head><body>404 Not Found: %s</body></html>"

/* the ports pacredir and pacserve listen to */
#define PORT_PACREDIR	7077
#define PORT_PACSERVE	7078

/* avahi service names */
#define PACSERVE	"_pacserve_" ID "_" ARCH "._tcp"

/* path to the config file */
#define CONFIGFILE	"/etc/pacredir.conf"
/* these characters are used as delimiter in config file */
#define DELIMITER	" ,;"

/* This defines the initial time in seconds after which a host is queried
 * again after a bad request. Time is doubled after every subsequent
 * request. */
#define BADTIME	30

#endif /* _CONFIG_H */

// vim: set syntax=c:
