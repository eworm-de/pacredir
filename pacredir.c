/*
 * (C) 2013 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * This is an example code skeleton provided by vim-skeleton.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include <curl/curl.h>
#include <microhttpd.h>

#define PAGE307 "<html><head><title>307 temporary redirect</title>" \
		"</head><body>307 temporary redirect: " \
		"<a href=\"%s\">%s</a></body></html>"
#define PAGE404 "<html><head><title>404 Not Found</title>" \
		"</head><body>404 Not Found: %s</body></html>"
#define PORT	7077
#define SERVICE	"_pacserve._tcp"

/* is there any other way than storing things in global struct? */
struct hosts {
	char * host;
	uint16_t port;
	struct hosts * next;
};

/* global variables */
struct hosts * hosts = NULL;
static AvahiSimplePoll *simple_poll = NULL;

static void resolve_callback(AvahiServiceResolver *r, AVAHI_GCC_UNUSED AvahiIfIndex interface, AVAHI_GCC_UNUSED AvahiProtocol protocol,
		AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host, const AvahiAddress *address,
		AVAHI_GCC_UNUSED uint16_t port, AVAHI_GCC_UNUSED AvahiStringList *txt, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
		AVAHI_GCC_UNUSED void* userdata) {
	struct hosts * tmphosts = hosts;

	assert(r);

	/* Called whenever a service has been resolved successfully or timed out */

	switch (event) {
		case AVAHI_RESOLVER_FAILURE:
			fprintf(stderr, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n",
				name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
			break;

		case AVAHI_RESOLVER_FOUND: {
			// char a[AVAHI_ADDRESS_STR_MAX];

			// avahi_address_snprint(a, sizeof(a), address);
			// fprintf(stderr, "%s %s\n", host, a);

			while (tmphosts->host != NULL) {
				if (strcmp(tmphosts->host, host) == 0) {
#					if defined DEBUG
					printf("Host is already in the list: %s\n", host);
#					endif
					goto out;
				}
				tmphosts = tmphosts->next;
			}
			printf("Adding host: %s, port %d\n", host, port);
			tmphosts->host = strdup(host);
			tmphosts->port = port;
			tmphosts->next = realloc(tmphosts->next, sizeof(struct hosts));
			tmphosts = tmphosts->next;
			tmphosts->host = NULL;
			tmphosts->next = NULL;
		}
	}

out:
	avahi_service_resolver_free(r);
}

static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name,
		const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata) {
	AvahiClient *c = userdata;

	assert(b);

	/* Called whenever a new services becomes available on the LAN or is removed from the LAN */

	switch (event) {
		case AVAHI_BROWSER_FAILURE:

			fprintf(stderr, "(Browser) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
			avahi_simple_poll_quit(simple_poll);
			return;

		case AVAHI_BROWSER_NEW:
			// fprintf(stderr, "(Browser) NEW: service '%s' of type '%s' in domain '%s'\n", name, type, domain);

			/* We ignore the returned resolver object. In the callback
			 * function we free it. If the server is terminated before
			 * the callback function is called the server will free
			 * the resolver for us. */

			if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, c)))
				fprintf(stderr, "Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));

			break;

		case AVAHI_BROWSER_REMOVE:
			// fprintf(stderr, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);

			/* TODO: remove host */
			// if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, c)))
			//	fprintf(stderr, "Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));

			break;

		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			// fprintf(stderr, "(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
			break;
	}
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
	assert(c);

	/* Called whenever the client or server state changes */

	if (state == AVAHI_CLIENT_FAILURE) {
		fprintf(stderr, "Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
		avahi_simple_poll_quit(simple_poll);
	}
}

int get_content(const char * host, const uint16_t port, const char * url) {
	CURL *curl;
	CURLcode res;
	unsigned int http_code;

	curl_global_init(CURL_GLOBAL_ALL);

	if ((curl = curl_easy_init()) != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		/* example.com is redirected, so we tell libcurl to follow redirection */ 
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		/* set user agent */
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "pacredir/" VERSION);
		/* do not receive body */
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

		/* get it! */
		if (curl_easy_perform(curl) != CURLE_OK) {
			fprintf(stderr, "Could not connect to server %s on port %d.\n", host, port);
			/* TODO remove or disable */
			return -1;
		}

		if ((res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code)) != CURLE_OK) {
			fprintf(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
			return -1;
		}

		// printf("%s: %d\n", url, http_code);

		/* always cleanup */ 
		curl_easy_cleanup(curl);
	}

	/* we're done with libcurl, so clean it up */
	curl_global_cleanup();

	return http_code;
}

static int ahc_echo(void * cls, struct MHD_Connection * connection, const char * uri, const char * method,
		const char * version, const char * upload_data, size_t * upload_data_size, void ** ptr) {
	static int dummy;
	struct MHD_Response * response;
	int ret;
	struct hosts * tmphosts = hosts;

	char * url = NULL, * page;
	const char * basename;
	int http_code = 0;

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
	*ptr = NULL; /* clear context pointer */

	/* try to find a server */
	while (tmphosts->host != NULL) {
		url = malloc(10 + strlen(tmphosts->host) + 5 + strlen(basename));
		sprintf(url, "http://%s:%d/%s", tmphosts->host, tmphosts->port, basename);

		printf("Trying %s\n", url);
		if ((http_code = get_content(tmphosts->host, tmphosts->port, url)) == MHD_HTTP_OK) {
			break;
		}

		tmphosts = tmphosts->next;
	}

	/* give response */
	if (http_code == MHD_HTTP_OK) {
		printf("Redirecting to %s\n", url);
		page = malloc(strlen(PAGE307) + strlen(url) + strlen(basename) + 1);
		sprintf(page, PAGE307, url, basename);
		response = MHD_create_response_from_data(strlen(url), (void*) url, MHD_NO, MHD_NO);
		ret =  MHD_add_response_header(response, "Location", url);
		ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, response);
	} else {
		printf("File %s Not found, giving up.\n", basename);
		page = malloc(strlen(PAGE404) + strlen(basename) + 1);
		sprintf(page, PAGE404, basename);
		response = MHD_create_response_from_data(strlen(page), (void*) page, MHD_NO, MHD_NO);
		ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
	}
	MHD_destroy_response(response);

	free(page);
	if (url != NULL)
		free(url);

	return ret;
}

int main(int argc, char ** argv) {
	AvahiClient *client = NULL;
	AvahiServiceBrowser *sb = NULL;
	int error;
	int ret = 1;
	struct MHD_Daemon * mhd;

	printf("Starting pacredir/" VERSION "\n");

	/* allocate first struct element as dummy */
	hosts = malloc(sizeof(struct hosts));
	hosts->host = NULL;
	hosts->port = 0;
	hosts->next = NULL;

	/* Allocate main loop object */
	if (!(simple_poll = avahi_simple_poll_new())) {
		fprintf(stderr, "Failed to create simple poll object.\n");
		goto fail;
	}

	/* Allocate a new client */
	client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);

	/* Check wether creating the client object succeeded */
	if (!client) {
		fprintf(stderr, "Failed to create client: %s\n", avahi_strerror(error));
		goto fail;
	}

	/* Create the service browser */
	if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, SERVICE, NULL, 0, browse_callback, client))) {
		fprintf(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
		goto fail;
	}

	if ((mhd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, PORT, NULL, NULL, &ahc_echo, NULL, MHD_OPTION_END)) == NULL) {
		fprintf(stderr, "Could not start daemon on port %d.\n", PORT);
		return EXIT_FAILURE;
	}
	
	/* Run the main loop */
	avahi_simple_poll_loop(simple_poll);

	MHD_stop_daemon(mhd);

	ret = EXIT_SUCCESS;

fail:

	/* Cleanup things */
	if (sb)
		avahi_service_browser_free(sb);

	if (client)
		avahi_client_free(client);

	if (simple_poll)
		avahi_simple_poll_free(simple_poll);

	return ret;
}

// vim: set syntax=c:
