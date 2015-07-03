/*
 * (C) 2013-2015 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
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
#include <sys/stat.h>
#include <time.h>

/* Avahi headers */
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>

/* various headers needing linker options */
#include <curl/curl.h>
#include <iniparser.h>
#include <microhttpd.h>
#include <pthread.h>

/* compile time configuration */
#include "arch.h"
#include "config.h"
#include "version.h"

#define PROGNAME	"pacredir"

/* services */
struct services {
	/* network port */
	uint16_t port;
	/* true if host/service is online */
	uint8_t online;
	/* unix timestamp of last bad request */
	__time_t badtime;
	/* count the number of bad requests */
	unsigned int badcount;
};

/* hosts */
struct hosts {
	/* host name */
	char * host;
	/* online status and bad time for services */
	struct services pacserve;
	struct services pacdbserve;
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
	/* host name */
	const char * host;
	/* pointer to service */
	struct services * service;
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
char * get_url(const char * hostname, const uint16_t port, const char * uri);

/* add_host */
int add_host(const char * host, const uint16_t port, const char * type);
/* remove_host */
int remove_host(const char * host, const char * type);

/* browse_callback */
static void browse_callback(AvahiServiceBrowser *b,
		AvahiIfIndex interface,
		AvahiProtocol protocol,
		AvahiBrowserEvent event,
		const char *name,
		const char *type,
		const char *domain,
		AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
		void* userdata);
/* client_callback */
static void client_callback(AvahiClient *c,
		AvahiClientState state,
		AVAHI_GCC_UNUSED void * userdata);

/* get_http_code */
static void * get_http_code(void * data);
/* ahc_echo */
static int ahc_echo(void * cls,
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
