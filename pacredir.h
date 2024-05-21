/*
 * (C) 2013-2024 by Christian Hesse <mail@eworm.de>
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
#include <systemd/sd-daemon.h>

/* Avahi headers */
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

/* various headers needing linker options */
#include <curl/curl.h>
#include <iniparser/iniparser.h>
#include <microhttpd.h>
#include <pthread.h>

/* compile time configuration */
#include "config.h"
#include "version.h"

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
	/* protocol (AVAHI_PROTO_INET, AVAHI_PROTO_INET6 or AVAHI_PROTO_UNSPEC) */
	AvahiProtocol proto;
	/* resolved address */
	char address[AVAHI_ADDRESS_STR_MAX];
	/* network port */
	uint16_t port;
	/* true if host/service is online */
	uint8_t online;
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
int write_log(FILE *stream, const char *format, ...);
/* get_fqdn */
char * get_fqdn(const char * hostname, const char * domainname);
/* get_url */
char * get_url(const char * hostname, AvahiProtocol proto, const char * address, const uint16_t port, const uint8_t dbfile, const char * uri);

/* add_host */
int add_host(const char * host, AvahiProtocol proto, const char * address, const uint16_t port, const char * type);
/* remove_host */
int remove_host(const char * host, AvahiProtocol proto, const char * type);

/* resolve_callback */
static void resolve_callback(AvahiServiceResolver *r,
		AvahiIfIndex interface,
		AvahiProtocol protocol,
		AvahiResolverEvent event,
		const char *name,
		const char *type,
		const char *domain,
		const char *host,
		const AvahiAddress *address,
		uint16_t port,
		AvahiStringList *txt,
		AvahiLookupResultFlags flags,
		void* userdata);
/* browse_callback */
static void browse_callback(AvahiServiceBrowser *b,
		AvahiIfIndex interface,
		AvahiProtocol protocol,
		AvahiBrowserEvent event,
		const char *name,
		const char *type,
		const char *domain,
		AvahiLookupResultFlags flags,
		void* userdata);
/* client_callback */
static void client_callback(AvahiClient *c,
		AvahiClientState state,
		void * userdata);

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
void sig_callback(int signal);
/* sighup_callback */
void sighup_callback(int signal);

#endif /* _PACREDIR_H */

// vim: set syntax=c:
