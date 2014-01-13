/*
 * (C) 2013-2014 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

/* glibc headers */
#include <arpa/inet.h>
#include <assert.h>
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

/* define structs and functions */
#include "pacredir.h"

/* global variables */
struct hosts * hosts = NULL;
struct ignore_interfaces * ignore_interfaces = NULL;
static AvahiSimplePoll *simple_poll = NULL;

/*** write_log ***/
int write_log(FILE *stream, const char *format, ...) {
	va_list args;
	va_start(args, format);

	vfprintf(stream, format, args);
	fflush(stream);

	return EXIT_SUCCESS;
}

/*** get_fqdn ***/
char * get_fqdn(const char * hostname, const char * domainname) {
	char * name;

	name = malloc(strlen(hostname) + strlen(domainname) + 2 /* '.' and null char */);
	sprintf(name, "%s.%s", hostname, domainname);

	return name;
}

/*** get_url ***/
char * get_url(const char * hostname, const uint16_t port, const char * uri) {
	char * url;

	url = malloc(10 /* static chars of an url & null char */
			+ strlen(hostname)
			+ 5 /* max strlen of decimal 16bit value */
			+ strlen(uri));
	sprintf(url, "http://%s:%d/%s",
			hostname, port, uri);

	return url;
}

/*** add_host ***/
int add_host(const char * host, const char * type) {
	struct hosts * tmphosts = hosts;
	struct request request;

	while (tmphosts->host != NULL) {
		if (strcmp(tmphosts->host, host) == 0) {
			/* host already exists */
			write_log(stdout, "Updating service %s on %s\n", type, host);
			goto update;
		}
		tmphosts = tmphosts->next;
	}
	/* host not found, adding a new one */
	write_log(stdout, "Adding host %s with service %s\n", host, type);
	tmphosts->host = strdup(host);
	tmphosts->pacserve.online = 0;
	tmphosts->pacserve.badtime = 0;
	tmphosts->pacserve.badcount = 0;
	tmphosts->pacdbserve.online = 0;
	tmphosts->pacdbserve.badtime = 0;
	tmphosts->pacdbserve.badcount = 0;
	tmphosts->next = malloc(sizeof(struct hosts));
	tmphosts->next->host = NULL;
	tmphosts->next->next = NULL;

update:
	if (strcmp(type, PACSERVE) == 0) {
		tmphosts->pacserve.online = 1;
		request.port = PORT_PACSERVE;
		request.service = &tmphosts->pacserve;
	} else if (strcmp(type, PACDBSERVE) == 0)  {
		tmphosts->pacdbserve.online = 1;
		request.port = PORT_PACDBSERVE;
		request.service = &tmphosts->pacdbserve;
	}

	/* do a first request and let get_http_code() set the bad status */
	request.host = tmphosts->host;
	request.url = get_url(request.host, request.port, "");
	request.http_code = 0;
	request.last_modified = 0;
	get_http_code(&request);
	free(request.url);

	return EXIT_SUCCESS;
}

/*** remove_host ***/
int remove_host(const char * host, const char * type) {
	struct hosts * tmphosts = hosts;

	while (tmphosts->host != NULL) {
		if (strcmp(tmphosts->host, host) == 0) {
			write_log(stdout, "Marking service %s on host %s offline\n", type, host);
			if (strcmp(type, PACSERVE) == 0) {
				tmphosts->pacserve.online = 0;
			} else if (strcmp(type, PACDBSERVE) == 0) {
				tmphosts->pacdbserve.online = 0;
			}
			break;
		}
		tmphosts = tmphosts->next;
	}

	return EXIT_SUCCESS;
}

/*** browse_callback ***
 * Called whenever a new services becomes available on the LAN or is removed from the LAN */
static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name,
		const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata) {
	char * host;
	char intname[IFNAMSIZ];
	struct ignore_interfaces * tmp_ignore_interfaces = ignore_interfaces;

	assert(b);

	switch (event) {
		case AVAHI_BROWSER_FAILURE:

			write_log(stderr, "%s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
			avahi_simple_poll_quit(simple_poll);
			return;

		case AVAHI_BROWSER_NEW:
			host = get_fqdn(name, domain);

			if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
				goto out;

			/* check whether to ignore the interface */
			if_indextoname(interface, intname);
			while (tmp_ignore_interfaces->next != NULL) {
				if (strcmp(intname, tmp_ignore_interfaces->interface) == 0) {
						write_log(stdout, "Ignoring service '%s' of type '%s' in domain '%s' on interface '%s'\n", name, type, domain, intname);
						goto out;
				}
				tmp_ignore_interfaces = tmp_ignore_interfaces->next;
			}

#			if defined DEBUG
			write_log(stdout, "NEW: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
#			endif

			add_host(host, type);
out:
			free(host);

			break;

		case AVAHI_BROWSER_REMOVE:
			host = get_fqdn(name, domain);

#			if defined DEBUG
			write_log(stdout, "REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
#			endif

			remove_host(host, type);

			free(host);

			break;

		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			break;
	}
}

/*** client_callback ***/
static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
	assert(c);

	if (state == AVAHI_CLIENT_FAILURE) {
		write_log(stderr, "Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
		avahi_simple_poll_quit(simple_poll);
	}
}

/*** get_http_code ***/
static void * get_http_code(void * data) {
	struct request * request = (struct request *)data;
	CURL *curl;
	CURLcode res;
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if ((curl = curl_easy_init()) != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, request->url);
		/* example.com is redirected, so we tell libcurl to follow redirection */
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		/* set user agent */
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "pacredir/" VERSION " (" ARCH ")");
		/* do not receive body */
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
		/* ask for filetime */
		curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
		/* set connection timeout to 5 seconds
		 * if the host needs longer we do not want to use it anyway ;) */
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
		/* time out if connection is established but transfer rate is low
		 * this should make curl finish after a maximum of 8 seconds */
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 3L);

		/* perform the request */
		if (curl_easy_perform(curl) != CURLE_OK) {
			write_log(stderr, "Could not connect to server %s on port %d.\n", request->host, request->port);
			request->http_code = 0;
			request->last_modified = 0;
			request->service->badtime = tv.tv_sec;
			request->service->badcount++;
			return NULL;
		} else {
			request->service->badtime = 0;
			request->service->badcount = 0;
		}


		/* get http status code */
		if ((res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(request->http_code))) != CURLE_OK) {
			write_log(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
			return NULL;
		}

		if ((res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &(request->time_total))) != CURLE_OK) {
			write_log(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
			return NULL;
		}

		/* get last modified time */
		if (request->http_code == MHD_HTTP_OK) {
			if ((res = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(request->last_modified))) != CURLE_OK) {
				write_log(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
				return NULL;
			}
		} else
			request->last_modified = 0;

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	return NULL;
}

/*** ahc_echo ***
 * called whenever a http request is received */
static int ahc_echo(void * cls, struct MHD_Connection * connection, const char * uri, const char * method,
		const char * version, const char * upload_data, size_t * upload_data_size, void ** ptr) {
	static int dummy;
	struct MHD_Response * response;
	int ret;
	struct hosts * tmphosts = hosts;

	char * url = NULL, * page;
	const char * basename;
	struct timeval tv;

	struct stat fst;
	char * filename;
	uint8_t dbfile = 0;
	int i, error, req_count = -1;
	pthread_t * tid = NULL;
	struct request ** requests = NULL;
	struct request * request = NULL;
	long http_code = 0, last_modified = 0;
	double time_total = INFINITY;

	/* we want the filename, not the path */
	basename = uri;
	while (strstr(basename, "/") != NULL)
		basename = strstr(basename, "/") + 1;

	/* unexpected method */
	if (strcmp(method, "GET") != 0)
		return MHD_NO;

	/* The first time only the headers are valid,
	 * do not respond in the first round... */
	if (&dummy != *ptr) {
		*ptr = &dummy;
		return MHD_YES;
	}

	/* upload data in a GET!? */
	if (*upload_data_size != 0)
		return MHD_NO;

	/* clear context pointer */
	*ptr = NULL;

	/* process db file (and signature) request */
	if ((strlen(basename) > 3 && strcmp(basename + strlen(basename) - 3, ".db") == 0) ||
			(strlen(basename) > 7 && strcmp(basename + strlen(basename) - 7, ".db.sig") == 0)) {
		dbfile = 1;
		/* get timestamp of local file */
		filename = malloc(strlen(SYNCPATH) + strlen(basename) + 2);
		sprintf(filename, SYNCPATH "/%s", basename);

		if (stat(filename, &fst) != 0)
			write_log(stderr, "stat() failed, you do not have a local copy of %s\n", basename);
		else
			last_modified = fst.st_mtime;

		free(filename);
	}

	/* try to find a server with most recent file */
	while (tmphosts->host != NULL) {
		gettimeofday(&tv, NULL);

		/* skip host if offline or had a bad request within last BADTIME seconds */
		if ((dbfile == 1 && (tmphosts->pacdbserve.online == 0 || tmphosts->pacdbserve.badtime + tmphosts->pacdbserve.badcount * BADTIME > tv.tv_sec)) ||
				(dbfile == 0 && (tmphosts->pacserve.online == 0 || tmphosts->pacserve.badtime + tmphosts->pacserve.badcount * BADTIME > tv.tv_sec))) {
			tmphosts = tmphosts->next;
			continue;
		}

		/* This is multi-threading code!
		 * Pointer to struct request does not work as realloc can relocate the data.
		 * We need a pointer to pointer to struct request, store the addresses in
		 * an array and give get_http_code() a struct the does not change! */
		req_count++;
		tid = realloc(tid, sizeof(pthread_t) * (req_count + 1));
		requests = realloc(requests, sizeof(size_t) * (req_count + 1));
		requests[req_count] = malloc(sizeof(struct request));
		request = requests[req_count];

		/* prepare request struct */
		request->host = tmphosts->host;
		if (dbfile == 1) {
			request->port = PORT_PACDBSERVE;
			request->service = &(tmphosts->pacdbserve);
		} else {
			request->port = PORT_PACSERVE;
			request->service = &(tmphosts->pacserve);
		}
		request->url = get_url(tmphosts->host, dbfile == 1 ? PORT_PACDBSERVE : PORT_PACSERVE, basename);
		request->http_code = 0;
		request->last_modified = 0;

		write_log(stdout, "Trying %s\n", request->url);

		if ((error = pthread_create(&tid[req_count], NULL, get_http_code, (void *)request)) != 0)
			write_log(stderr, "Could not run thread number %d, errno %d\n", req_count, error);

		tmphosts = tmphosts->next;
	}

	/* try to find a suitable response */
	for (i = 0; i <= req_count; i++) {
		if ((error = pthread_join(tid[i], NULL)) != 0)
			write_log(stderr, "Could not join thread number %d, errno %d\n", i, error);

		request = requests[i];

		if (request->http_code == MHD_HTTP_OK)
			printf("Found: %s (%f sec)\n", request->url, request->time_total);

		if (request->http_code == MHD_HTTP_OK &&
				/* for db files choose the most recent server */
				((dbfile == 1 && request->last_modified > last_modified) ||
				 /* for packages try to guess the fastest server */
				 (dbfile == 0 && request->time_total < time_total))) {
			if (url != NULL)
				free(url);
			url = request->url;
			http_code = MHD_HTTP_OK;
			last_modified = request->last_modified;
			time_total = request->time_total;
		} else
			free(request->url);
		free(request);
	}

	/* give response */
	if (http_code == MHD_HTTP_OK) {
		write_log(stdout, "Redirecting to %s\n", url);
		page = malloc(strlen(PAGE307) + strlen(url) + strlen(basename) + 1);
		sprintf(page, PAGE307, url, basename + 1);
		response = MHD_create_response_from_data(strlen(page), (void*) page, MHD_NO, MHD_NO);
		ret = MHD_add_response_header(response, "Location", url);
		ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, response);
		free(url);
	} else {
		write_log(stdout, "File %s not found on %d servers, giving up.\n", basename, req_count + 1);
		page = malloc(strlen(PAGE404) + strlen(basename) + 1);
		sprintf(page, PAGE404, basename + 1);
		response = MHD_create_response_from_data(strlen(page), (void*) page, MHD_NO, MHD_NO);
		ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
	}
	MHD_destroy_response(response);

	free(page);
	if (req_count > -1) {
		free(tid);
		free(requests);
	}

	return ret;
}

/*** sig_callback ***/
void sig_callback(int signal) {
	write_log(stdout, "Received signal '%s', quitting.\n", strsignal(signal));

	avahi_simple_poll_quit(simple_poll);
}

/*** sighup_callback ***/
void sighup_callback(int signal) {
	struct hosts * tmphosts = hosts;

	write_log(stdout, "Received SIGHUP, resetting bad status for hosts.\n");

	while (tmphosts->host != NULL) {
		tmphosts->pacserve.badtime = 0;
		tmphosts->pacserve.badcount = 0;
		tmphosts->pacdbserve.badtime = 0;
		tmphosts->pacdbserve.badcount = 0;
		tmphosts = tmphosts->next;
	}
}

/*** main ***/
int main(int argc, char ** argv) {
	dictionary * ini;
	char * values, * value;
	struct ignore_interfaces * tmp_ignore_interfaces;
	AvahiClient *client = NULL;
	AvahiServiceBrowser *pacserve = NULL, *pacdbserve = NULL;
	int error;
	int ret = 1;
	struct MHD_Daemon * mhd;
	struct hosts * tmphosts;
	struct sockaddr_in address;

	write_log(stdout, "Starting pacredir/" VERSION " (compiled: " __DATE__ ", " __TIME__ " for " ARCH ")\n");

	/* allocate first struct element as dummy */
	hosts = malloc(sizeof(struct hosts));
	hosts->host = NULL;
	hosts->pacserve.online = 0;
	hosts->pacserve.badtime = 0;
	hosts->pacdbserve.online = 0;
	hosts->pacdbserve.badtime = 0;
	hosts->next = NULL;

	ignore_interfaces = malloc(sizeof(struct ignore_interfaces));
	ignore_interfaces->interface = NULL;
	ignore_interfaces->next = NULL;

	/* parse config file */
	if ((ini = iniparser_load(CONFIGFILE)) == NULL) {
		write_log(stderr, "cannot parse file " CONFIGFILE ", continue anyway\n");
	/* continue anyway, there is nothing essential in the config file */
	} else {
		/* store interfaces to ignore */
		if ((values = iniparser_getstring(ini, "general:ignore interfaces", NULL)) != NULL) {
#			if defined DEBUG
			write_log(stdout, "Ignore interface: [%s]\n", values);
#			endif
			tmp_ignore_interfaces = ignore_interfaces;

			value = strtok(values, DELIMITER);
			while (value != NULL) {
				tmp_ignore_interfaces->interface = strdup(value);
				tmp_ignore_interfaces->next = malloc(sizeof(struct ignore_interfaces));
				tmp_ignore_interfaces = tmp_ignore_interfaces->next;
				value = strtok(NULL, DELIMITER);
			}
			tmp_ignore_interfaces->interface = NULL;
			tmp_ignore_interfaces->next = NULL;
		}

		/* add static pacserve hosts */
		if ((values = iniparser_getstring(ini, "general:pacserve hosts", NULL)) != NULL) {
#			if defined DEBUG
			write_log(stdout, "pacserve hosts: [%s]\n", values);
#			endif
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				add_host(value, PACSERVE);
				value = strtok(NULL, DELIMITER);
			}
		}

		/* add static pacdbserve hosts */
		if ((values = iniparser_getstring(ini, "general:pacdbserve hosts", NULL)) != NULL) {
#			if defined DEBUG
			write_log(stdout, "pacdbserve hosts: [%s]\n", values);
#			endif
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				add_host(value, PACDBSERVE);
				value = strtok(NULL, DELIMITER);
			}
		}

		/* done reading config file, free */
		iniparser_freedict(ini);
	}

	/* allocate main loop object */
	if ((simple_poll = avahi_simple_poll_new()) == NULL) {
		write_log(stderr, "Failed to create simple poll object.\n");
		goto fail;
	}

	/* allocate a new client */
	if ((client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error)) == NULL) {
		write_log(stderr, "Failed to create client: %s\n", avahi_strerror(error));
		goto fail;
	}

	/* create the service browser for PACSERVE */
	if ((pacserve = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, PACSERVE, NULL, 0, browse_callback, NULL)) == NULL) {
		write_log(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
		goto fail;
	}

	/* create the service browser for PACDBSERVE */
	if ((pacdbserve = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, PACDBSERVE, NULL, 0, browse_callback, NULL)) == NULL) {
		write_log(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
		goto fail;
	}

	/* prepare struct to make microhttpd listen on localhost only */
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT_PACREDIR);
	inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

	/* start http server */
	if ((mhd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT_PACREDIR, NULL, NULL, &ahc_echo, NULL, MHD_OPTION_SOCK_ADDR, &address, MHD_OPTION_END)) == NULL) {
		write_log(stderr, "Could not start daemon on port %d.\n", PORT_PACREDIR);
		return EXIT_FAILURE;
	}

	/* initialize curl */
	curl_global_init(CURL_GLOBAL_ALL);

	/* register signal callbacks */
	signal(SIGTERM, sig_callback);
	signal(SIGINT, sig_callback);
	signal(SIGHUP, sighup_callback);

	/* run the main loop */
	avahi_simple_poll_loop(simple_poll);

	/* stop http server */
	MHD_stop_daemon(mhd);

	/* we're done with libcurl, so clean it up */
	curl_global_cleanup();

	ret = EXIT_SUCCESS;

fail:

	/* Cleanup things */
	while (hosts->host != NULL) {
		free(hosts->host);
		tmphosts = hosts->next;
		free(hosts);
		hosts = tmphosts;
	}

	while (ignore_interfaces->interface != NULL) {
		free(ignore_interfaces->interface);
		tmp_ignore_interfaces = ignore_interfaces->next;
		free(ignore_interfaces);
		ignore_interfaces = tmp_ignore_interfaces;
	}

	if (pacdbserve)
		avahi_service_browser_free(pacdbserve);

	if (pacserve)
		avahi_service_browser_free(pacserve);

	if (client)
		avahi_client_free(client);

	if (simple_poll)
		avahi_simple_poll_free(simple_poll);

	return ret;
}

// vim: set syntax=c:
