/*
 * (C) 2013-2025 by Christian Hesse <mail@eworm.de>
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

#ifndef _PACREDIR_H
#define _PACREDIR_H

#define _GNU_SOURCE

/* glibc headers */
#include <arpa/inet.h>
#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

/* systemd headers */
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

/* various headers needing linker options */
#include <curl/curl.h>
#include <iniparser/iniparser.h>
#include <microhttpd.h>
#include <pthread.h>

/* compile time configuration */
#include "config.h"
#include "version.h"

#define DNS_CLASS_IN 1U
#define DNS_TYPE_PTR 12U

#define SD_RESOLVED_NO_SYNTHESIZE	(UINT64_C(1) << 11)
#define SD_RESOLVED_NO_ZONE		(UINT64_C(1) << 13)

#define PROGNAME	"pacredir"

/* libmicrohttpd compat */
#if MHD_VERSION >= 0x00097002
#  define mhd_result enum MHD_Result
#else
#  define mhd_result int
#endif

/* hosts */
struct hosts {
	/* host name */
	char * host;
	/* network port */
	uint16_t port;
	/* true for hosts from mDNS (vs. static) */
	uint8_t mdns;
	/* true if host/service is online */
	uint8_t online;
	/* intermediate state while querying mDNS */
	uint8_t present;
	/* unix timestamp of last bad request */
	__time_t badtime;
	/* count the number of bad requests */
	unsigned int badcount;
	/* pointer to next struct element */
	struct hosts * next;
};

/* ignore interfaces */
struct ignore_interfaces {
	/* interface name */
	char * interface;
	/* interface index */
	unsigned int ifindex;
	/* pointer to next struct element */
	struct ignore_interfaces * next;
};

/* request */
struct request {
	/* host infos */
	struct hosts * host;
	/* url */
	char * url;
	/* HTTP status code */
	long http_code;
	/* total connection time */
	double time_total;
	/* last modified timestamp */
	long last_modified;
};

/* write_log */
static int write_log(FILE *stream, const char *format, ...);
/* get_url */
static char * get_url(const char * hostname, const uint16_t port, const uint8_t dbfile, const char * uri);
/* update_interfaces */
static void update_interfaces(void);

/* get_name */
static size_t get_name(const uint8_t* rr_ptr, char* name);
/* process_reply_record */
static char* process_reply_record(const void *rr, size_t sz);
/* update_hosts */
static void update_hosts(void);

/* add_host */
static int add_host(const char * host, const uint16_t port, const uint8_t mdns);
/* remove_host */
/* static int remove_host(const char * host); */

/* get_http_code */
static void * get_http_code(void * data);
/* ahc_echo */
static mhd_result ahc_echo(void * cls,
		struct MHD_Connection * connection,
		const char * uri,
		const char * method,
		const char * version,
		const char * upload_data,
		size_t * upload_data_size,
		void ** ptr);

/* sig_callback */
static void sig_callback(int signal);
/* sighup_callback */
static void sighup_callback(int signal);

#endif /* _PACREDIR_H */

// vim: set syntax=c:
