/*
 * (C) 2013 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include <curl/curl.h>
#include <microhttpd.h>

#include "config.h"

/* services */
struct services {
	/* true if host/service is online */
	uint8_t online;
	/* unix timestamp of last bad request */
	__time_t bad;
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

/* global variables */
struct hosts * hosts = NULL;
static AvahiSimplePoll *simple_poll = NULL;

/*** get_fqdn ***/
char * get_fqdn(const char * hostname, const char * domainname) {
	char * name;

	name = malloc(strlen(hostname) + strlen(domainname) + 2 /* '.' and null char */);
	sprintf(name, "%s.%s", hostname, domainname);
	return name;
}

/*** browse_callback ***
 * Called whenever a new services becomes available on the LAN or is removed from the LAN */
static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name,
		const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata) {
	struct hosts * tmphosts = hosts;
	char * host;

	assert(b);
			
	switch (event) {
		case AVAHI_BROWSER_FAILURE:

			fprintf(stderr, "%s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
			avahi_simple_poll_quit(simple_poll);
			return;

		case AVAHI_BROWSER_NEW:
			host = get_fqdn(name, domain);

#			if defined DEBUG
			printf("NEW: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
#			endif

			if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
				goto out;

			while (tmphosts->host != NULL) {
				if (strcmp(tmphosts->host, host) == 0) {
					/* host already exists */
					printf("Updating service %s on %s\n", type, host);
					goto update;
				}
				tmphosts = tmphosts->next;
			}
			/* host not found, adding a new one */
			printf("Adding host %s with service %s\n", host, type);
			tmphosts->host = strdup(host);
			tmphosts->pacserve.online = 0;
			tmphosts->pacserve.bad = 0;
			tmphosts->pacdbserve.online = 0;
			tmphosts->pacdbserve.bad = 0;
			tmphosts->next = realloc(tmphosts->next, sizeof(struct hosts));
			tmphosts->next->host = NULL;
			tmphosts->next->next = NULL;

update:
			if (strcmp(type, PACSERVE) == 0) {
				tmphosts->pacserve.online = 1;
				tmphosts->pacserve.bad = 0;
			} else if (strcmp(type, PACDBSERVE) == 0)  {
				tmphosts->pacdbserve.online = 1;
				tmphosts->pacdbserve.bad = 0;
			}

out:
			free(host);

			break;

		case AVAHI_BROWSER_REMOVE:
			host = get_fqdn(name, domain);

#			if defined DEBUG
			printf("REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
#			endif

			while (tmphosts->host != NULL) {
				if (strcmp(tmphosts->host, host) == 0) {
					printf("Marking service %s on host %s offline\n", type, host);
					if (strcmp(type, PACSERVE) == 0) {
						tmphosts->pacserve.online = 0;
					} else if (strcmp(type, PACDBSERVE) == 0) {
						tmphosts->pacdbserve.online = 0;
					}
					break;
				}
				tmphosts = tmphosts->next;
			}

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
		fprintf(stderr, "Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
		avahi_simple_poll_quit(simple_poll);
	}
}

/*** get_http_code ***/
int get_http_code(const char * host, const uint16_t port, const char * url, int * http_code, int * last_modified) {
	CURL *curl;
	CURLcode res;

	curl_global_init(CURL_GLOBAL_ALL);

	if ((curl = curl_easy_init()) != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		/* example.com is redirected, so we tell libcurl to follow redirection */ 
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		/* set user agent */
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "pacredir/" VERSION);
		/* do not receive body */
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
		/* ask for filetime */
		curl_easy_setopt(curl, CURLOPT_FILETIME, 1);
		/* set connection timeout to 2 seconds
		 * if the host needs longer we do not want to use it anyway ;) */
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2);

		/* perform the request */
		if (curl_easy_perform(curl) != CURLE_OK) {
			fprintf(stderr, "Could not connect to server %s on port %d.\n", host, port);
			*http_code = 0;
			*last_modified = 0;
			return EXIT_FAILURE;
		}

		/* get http code */
		if ((res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code)) != CURLE_OK) {
			fprintf(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
			return EXIT_FAILURE;
		}

		/* get last modified time */
		if (*http_code == MHD_HTTP_OK) {
			if ((res = curl_easy_getinfo(curl, CURLINFO_FILETIME, last_modified)) != CURLE_OK) {
				fprintf(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
				return EXIT_FAILURE;
			}
		} else
			*last_modified = 0;

		/* always cleanup */ 
		curl_easy_cleanup(curl);
	}

	/* we're done with libcurl, so clean it up */
	curl_global_cleanup();

	return EXIT_SUCCESS;
}

/*** ahc_echo ***
 * called whenever a http request is received */
static int ahc_echo(void * cls, struct MHD_Connection * connection, const char * uri, const char * method,
		const char * version, const char * upload_data, size_t * upload_data_size, void ** ptr) {
	static int dummy;
	struct MHD_Response * response;
	int ret;
	struct hosts * tmphosts = hosts;

	char * url = NULL, * url_recent = NULL, * page;
	const char * basename;
	struct timeval tv;

	struct stat fst;
	char * filename;
	int http_code, recent = 0;
	int last_modified, last_modified_recent = 0;

	/* we want to filename, not the path */
	basename = uri;
	while (strstr(basename, "/") != NULL)
		basename = strstr(basename, "/") + 1;

	if (strcmp(method, "GET") != 0)
		return MHD_NO; /* unexpected method */
	if (&dummy != *ptr) {
		/* The first time only the headers are valid,
		 * do not respond in the first round... */
		*ptr = &dummy;
		return MHD_YES;
	}
	if (*upload_data_size != 0)
		return MHD_NO; /* upload data in a GET!? */

	/* clear context pointer */
	*ptr = NULL;

	/* process db file request */
	if (strlen(basename) > 3 && strcmp(basename + strlen(basename) - 3, ".db") == 0) {
		/* get timestamp of local file */
		filename = malloc(strlen(SYNCPATH) + strlen(basename) + 2);
		sprintf(filename, SYNCPATH "/%s", basename);
	
		bzero(&fst, sizeof(fst));
		if (stat(filename, &fst) != 0)
			fprintf(stderr, "stat() failed, you do not have a local copy of %s\n", basename);
		else
			last_modified_recent = fst.st_mtime;

		free(filename);

		/* try to find a server with most recent file */
		while (tmphosts->host != NULL) {
			gettimeofday(&tv, NULL);

			/* skip host if offline or had a bad request within last BADTIME seconds */
			if (tmphosts->pacdbserve.online == 0 || tmphosts->pacdbserve.bad + BADTIME > tv.tv_sec) {
				tmphosts = tmphosts->next;
				continue;
			}

			url = realloc(url, 10 + strlen(tmphosts->host) + (log10(PORT_PACDBSERVE)+1) + strlen(basename));
			sprintf(url, "http://%s:%d/%s", tmphosts->host, PORT_PACDBSERVE, basename);

			printf("Trying %s\n", url);
			if (get_http_code(tmphosts->host, PORT_PACDBSERVE, url, &http_code, &last_modified) == EXIT_FAILURE)
				tmphosts->pacdbserve.bad = tv.tv_sec;
			else if (http_code == MHD_HTTP_OK && last_modified > last_modified_recent) {
				if (recent > 0)
					free(url_recent);
				last_modified_recent = last_modified;
				url_recent = url;
				url = NULL;
				recent++;
			}

			tmphosts = tmphosts->next;
		}
		if (url != NULL) {
			free(url);
			url = NULL;
		}
		if (recent > 0) {
			http_code = MHD_HTTP_OK;
			url = url_recent;
		} else
			http_code = 0;
	/* process package file request */
	} else {
		/* try to find a server */
		while (tmphosts->host != NULL) {
			gettimeofday(&tv, NULL);

			/* skip host if offline or had a bad request within last BADTIME seconds */
			if (tmphosts->pacserve.online == 0 || tmphosts->pacserve.bad + BADTIME > tv.tv_sec) {
				tmphosts = tmphosts->next;
				continue;
			}

			url = realloc(url, 10 + strlen(tmphosts->host) + (log10(PORT_PACSERVE)+1) + strlen(basename));
			sprintf(url, "http://%s:%d/%s", tmphosts->host, PORT_PACSERVE, basename);

			printf("Trying %s\n", url);
			if (get_http_code(tmphosts->host, PORT_PACSERVE, url, &http_code, &last_modified) == EXIT_FAILURE)
				tmphosts->pacserve.bad = tv.tv_sec;
			else if (http_code == MHD_HTTP_OK)
				break;

			tmphosts = tmphosts->next;
		}
	}

	/* give response */
	if (http_code == MHD_HTTP_OK) {
		printf("Redirecting to %s\n", url);
		page = malloc(strlen(PAGE307) + strlen(url) + strlen(basename) + 1);
		sprintf(page, PAGE307, url, basename + 1);
		response = MHD_create_response_from_data(strlen(page), (void*) page, MHD_NO, MHD_NO);
		ret = MHD_add_response_header(response, "Location", url);
		ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, response);
	} else {
		printf("File %s not found, giving up.\n", basename);
		page = malloc(strlen(PAGE404) + strlen(basename) + 1);
		sprintf(page, PAGE404, basename + 1);
		response = MHD_create_response_from_data(strlen(page), (void*) page, MHD_NO, MHD_NO);
		ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
	}
	MHD_destroy_response(response);

	free(page);
	if (url != NULL)
		free(url);

	return ret;
}

/*** sigterm_callback ***/
void sigterm_callback(int signal) {
	avahi_simple_poll_quit(simple_poll);
}

/*** sighup_callback ***/
void sighup_callback(int signal) {
	struct hosts * tmphosts = hosts;
	
	printf("Received SIGHUP, marking all hosts offline.\n");

	while (tmphosts->host != NULL) {
		tmphosts->pacserve.online = 0;
		tmphosts->pacdbserve.online = 0;
		tmphosts = tmphosts->next;
	}
}

/*** main ***/
int main(int argc, char ** argv) {
	AvahiClient *client = NULL;
	AvahiServiceBrowser *pacserve = NULL, *pacdbserve = NULL;
	int error;
	int ret = 1;
	struct MHD_Daemon * mhd;
	struct hosts * tmphosts;
	struct sockaddr_in address;

	printf("Starting pacredir/" VERSION "\n");

	/* allocate first struct element as dummy */
	hosts = malloc(sizeof(struct hosts));
	hosts->host = NULL;
	hosts->pacserve.online = 0;
	hosts->pacserve.bad = 0;
	hosts->pacdbserve.online = 0;
	hosts->pacdbserve.bad = 0;
	hosts->next = NULL;

	/* allocate main loop object */
	if (!(simple_poll = avahi_simple_poll_new())) {
		fprintf(stderr, "Failed to create simple poll object.\n");
		goto fail;
	}

	/* allocate a new client */
	client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);

	/* check wether creating the client object succeeded */
	if (!client) {
		fprintf(stderr, "Failed to create client: %s\n", avahi_strerror(error));
		goto fail;
	}

	/* create the service browser for PACSERVE */
	if ((pacserve = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, PACSERVE, NULL, 0, browse_callback, client)) == NULL) {
		fprintf(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
		goto fail;
	}

	/* create the service browser for PACDBSERVE */
	if ((pacdbserve = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, PACDBSERVE, NULL, 0, browse_callback, client)) == NULL) {
		fprintf(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
		goto fail;
	}

	/* prepare struct to make microhttpd listen on localhost only */
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT_PACREDIR);
	inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

	/* start http server */
	if ((mhd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT_PACREDIR, NULL, NULL, &ahc_echo, NULL, MHD_OPTION_SOCK_ADDR, &address, MHD_OPTION_END)) == NULL) {
		fprintf(stderr, "Could not start daemon on port %d.\n", PORT_PACREDIR);
		return EXIT_FAILURE;
	}

	/* register signal callbacks */
	signal(SIGTERM, sigterm_callback);
	signal(SIGHUP, sighup_callback);
	
	/* run the main loop */
	avahi_simple_poll_loop(simple_poll);

	MHD_stop_daemon(mhd);

	ret = EXIT_SUCCESS;

fail:

	/* Cleanup things */
	while(hosts->host != NULL) {
		free(hosts->host);
		tmphosts = hosts->next;
		free(hosts);
		hosts = tmphosts;
	}

	if (pacdbserve)
		avahi_service_browser_free(pacdbserve);

	if (pacserve)
		avahi_service_browser_free(pacdbserve);

	if (client)
		avahi_client_free(client);

	if (simple_poll)
		avahi_simple_poll_free(simple_poll);

	return ret;
}

// vim: set syntax=c:
