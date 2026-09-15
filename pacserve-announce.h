/*
 * (C) 2013-2026 by Christian Hesse <mail@eworm.de>
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

#ifndef _PACSERVE_ANNOUNCE_H
#define _PACSERVE_ANNOUNCE_H

#define _GNU_SOURCE

/* glibc headers */
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* systemd headers */
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

/* iniparser headers */
#include <iniparser/iniparser.h>

/* compile time configuration */
#include "config.h"
#include "version.h"

/* sig_callback */
static void sig_callback(int signal);

/* prepare message */
static int prepare_message(sd_bus_message *message, uint16_t port);

#endif /* _PACSERVE_ANNOUNCE_H */
