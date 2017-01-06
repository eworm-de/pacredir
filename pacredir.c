/*
 * (C) 2013-2016 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

/* define structs and functions */
#include "pacredir.h"

const static char optstring[] = "hvV";
const static struct option options_long[] = {
	/* name		has_arg		flag	val */
	{ "help",	no_argument,	NULL,	'h' },
	{ "verbose",	no_argument,	NULL,	'v' },
	{ "version",	no_argument,	NULL,	'V' },
	{ 0, 0, 0, 0 }
};

/* global variables */
struct hosts * hosts = NULL;
struct ignore_interfaces * ignore_interfaces = NULL;
int max_threads = 0;
static AvahiSimplePoll *simple_poll = NULL;
uint8_t verbose = 0;

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
int add_host(const char * host, const uint16_t port, const char * type) {
	struct hosts * tmphosts = hosts;
	struct request request;

	while (tmphosts->host != NULL) {
		if (strcmp(tmphosts->host, host) == 0) {
			/* host already exists */
			if (verbose > 0)
				write_log(stdout, "Updating service %s (port %d) on host %s\n",
						type, port, host);
			goto update;
		}
		tmphosts = tmphosts->next;
	}

	/* host not found, adding a new one */
	if (verbose > 0)
		write_log(stdout, "Adding host %s with service %s (port %d)\n",
				host, type, port);
	tmphosts->host = strdup(host);

	tmphosts->pacserve.port = 0;
	tmphosts->pacserve.online = 0;
	tmphosts->pacserve.badtime = 0;
	tmphosts->pacserve.badcount = 0;

	tmphosts->pacdbserve.port = 0;
	tmphosts->pacdbserve.online = 0;
	tmphosts->pacdbserve.badtime = 0;
	tmphosts->pacdbserve.badcount = 0;

	tmphosts->next = malloc(sizeof(struct hosts));
	tmphosts->next->host = NULL;
	tmphosts->next->next = NULL;

update:
	if (strcmp(type, PACSERVE) == 0) {
		tmphosts->pacserve.online = 1;
		tmphosts->pacserve.port = port;
		request.service = &tmphosts->pacserve;
	} else if (strcmp(type, PACDBSERVE) == 0) {
		tmphosts->pacdbserve.online = 1;
		tmphosts->pacdbserve.port = port;
		request.service = &tmphosts->pacdbserve;
	}

	/* do a first request and let get_http_code() set the bad status */
	request.host = tmphosts->host;
	request.url = get_url(request.host, request.service->port, "");
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
			if (verbose > 0)
				write_log(stdout, "Marking service %s on host %s offline\n",
						type, host);
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
static void browse_callback(AvahiServiceBrowser *b,
		AvahiIfIndex interface,
		AvahiProtocol protocol,
		AvahiBrowserEvent event,
		const char *name,
		const char *type,
		const char *domain,
		AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
		void* userdata) {
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
					if (verbose > 0)
						write_log(stdout, "Ignoring service %s on host %s on interface %s\n",
								type, host, intname);
					goto out;
				}
				tmp_ignore_interfaces = tmp_ignore_interfaces->next;
			}

			if (verbose > 0)
				write_log(stdout, "Found service %s on host %s on interface %s\n",
						type, host, intname);

			add_host(host, strcmp(type, PACSERVE) == 0 ? PORT_PACSERVE : PORT_PACDBSERVE, type);
out:
			free(host);

			break;

		case AVAHI_BROWSER_REMOVE:
			host = get_fqdn(name, domain);

			if (verbose > 0)
				write_log(stdout, "Service %s on host %s disappeared\n",
						type, host);

			remove_host(host, type);

			free(host);

			break;

		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			break;
	}
}

/*** client_callback ***/
static void client_callback(AvahiClient *c,
		AvahiClientState state,
		AVAHI_GCC_UNUSED void * userdata) {
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
	char errbuf[CURL_ERROR_SIZE];
	struct timeval tv;

	gettimeofday(&tv, NULL);

	if ((curl = curl_easy_init()) != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, request->url);
		/* try to resolve addresses to all IP versions that your system allows */
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
		/* tell libcurl to follow redirection */
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
		/* provide a buffer to store errors in */
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
		*errbuf = '\0';

		/* perform the request */
		if ((res = curl_easy_perform(curl)) != CURLE_OK) {
			write_log(stderr, "Could not connect to server %s on port %d: %s\n",
					request->host, request->service->port,
					*errbuf != 0 ? errbuf : curl_easy_strerror(res));
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
static int ahc_echo(void * cls,
		struct MHD_Connection * connection,
		const char * uri,
		const char * method,
		const char * version,
		const char * upload_data,
		size_t * upload_data_size,
		void ** ptr) {
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
	char ctime[26];

	/* initialize struct timeval */
	gettimeofday(&tv, NULL);

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

	/* redirect to website if no file given */
	if (*basename == 0) {
		http_code = MHD_HTTP_OK;
		/* duplicate string so we can free it later */
		url = strdup(WEBSITE);
		goto response;
	}

	/* process db file request (*.db and *.files) */
	if ((strlen(basename) > 3 && strcmp(basename + strlen(basename) - 3, ".db") == 0) ||
			(strlen(basename) > 6 && strcmp(basename + strlen(basename) - 6, ".files") == 0)) {
		dbfile = 1;
		/* get timestamp of local file */
		filename = malloc(strlen(SYNCPATH) + strlen(basename) + 2);
		sprintf(filename, SYNCPATH "/%s", basename);

		if (stat(filename, &fst) != 0) {
			if (verbose > 0)
				write_log(stdout, "You do not have a local copy of %s\n", basename);
		} else
			last_modified = fst.st_mtime;

		free(filename);
	}

	/* try to find a server with most recent file */
	while (tmphosts->host != NULL) {
		struct services *service = (dbfile ? &tmphosts->pacdbserve : &tmphosts->pacserve);
		time_t badtime = service->badtime + service->badcount * BADTIME;

		/* skip host if offline or had a bad request within last BADTIME seconds */
		if (service->online == 0) {
			if (verbose > 0)
				write_log(stdout, "Service %s on host %s is offline, skipping\n",
						dbfile ? PACDBSERVE : PACSERVE, tmphosts->host);
			tmphosts = tmphosts->next;
			continue;
		} else if (badtime > tv.tv_sec) {
			if (verbose > 0) {
				/* write the time to buffer ctime, then strip the line break */
				ctime_r(&badtime, ctime);
				ctime[strlen(ctime) - 1] = '\0';

				write_log(stdout, "Service %s on host %s is marked bad until %s, skipping\n",
						dbfile ? PACDBSERVE : PACSERVE, tmphosts->host, ctime);
			}
			tmphosts = tmphosts->next;
			continue;
		}

		/* Check for limit on threads */
		if (max_threads > 0 && req_count + 1 >= max_threads) {
			if (verbose > 0)
				write_log(stdout, "Hit hard limit for max threads (%d), not doing more requests\n",
						max_threads);
			break;
		}

		/* throttle requests - do not send all request at the same time
		 * but wait for a short moment (10.000 us = 0.01 s) */
		usleep(10000);

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
		if (dbfile == 1)
			request->service = &(tmphosts->pacdbserve);
		else
			request->service = &(tmphosts->pacserve);
		request->url = get_url(tmphosts->host, request->service->port, basename);
		request->http_code = 0;
		request->last_modified = 0;

		if (verbose > 0)
			write_log(stdout, "Trying: %s\n", request->url);

		if ((error = pthread_create(&tid[req_count], NULL, get_http_code, (void *)request)) != 0)
			write_log(stderr, "Could not run thread number %d, errno %d\n", req_count, error);

		tmphosts = tmphosts->next;
	}

	/* try to find a suitable response */
	for (i = 0; i <= req_count; i++) {
		if ((error = pthread_join(tid[i], NULL)) != 0)
			write_log(stderr, "Could not join thread number %d, errno %d\n", i, error);

		request = requests[i];

		if (request->http_code == MHD_HTTP_OK) {
			if (verbose > 0) {
				/* write the time to buffer ctime, then strip the line break */
				ctime_r(&request->last_modified, ctime);
				ctime[strlen(ctime) - 1] = '\0';

				write_log(stdout, "Found: %s (%f sec, modified: %s)\n",
						request->url, request->time_total, ctime);
			}
		} else if (verbose > 0 && request->http_code > 0) {
			if (verbose > 0)
				write_log(stderr, "Received HTTP status code %d for %s\n",
						request->http_code, request->url);
		}

		if (request->http_code == MHD_HTTP_OK &&
				/* for db files choose the most recent server */
				((dbfile == 1 && ((request->last_modified > last_modified) ||
				/* but use a faster server if available */
						  (url != NULL &&
						   request->last_modified >= last_modified &&
						   request->time_total < time_total))) ||
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

response:
	/* give response */
	if (http_code == MHD_HTTP_OK) {
		write_log(stdout, "Redirecting to %s\n", url);
		page = malloc(strlen(PAGE307) + strlen(url) + strlen(basename) + 1);
		sprintf(page, PAGE307, url, basename);
		response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_PERSISTENT);
		ret = MHD_add_response_header(response, "Location", url);
		ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, response);
		free(url);
	} else {
		if (req_count < 0)
			write_log(stdout, "Currently no servers are available to check for %s.\n",
					basename);
		else if (dbfile == 1)
			write_log(stdout, "No more recent version of %s found on %d servers.\n",
					basename, req_count + 1);
		else
			write_log(stdout, "File %s not found on %d servers, giving up.\n",
					basename, req_count + 1);

		page = malloc(strlen(PAGE404) + strlen(basename) + 1);
		sprintf(page, PAGE404, basename);
		response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_PERSISTENT);
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
	const char * inistring;
	char * values, * value;
	uint16_t port;
	struct ignore_interfaces * tmp_ignore_interfaces;
	AvahiClient *client = NULL;
	AvahiServiceBrowser *pacserve = NULL, *pacdbserve = NULL;
	int error, i, ret = 1;
	struct MHD_Daemon * mhd;
	struct hosts * tmphosts;
	struct sockaddr_in address;

	unsigned int version = 0, help = 0;

	/* get the verbose status */
	while ((i = getopt_long(argc, argv, optstring, options_long, NULL)) != -1) {
		switch (i) {
			case 'h':
				help++;
				break;
			case 'v':
				verbose++;
				break;
			case 'V':
				verbose++;
				version++;
				break;
		}
	}

	if (verbose > 0)
		write_log(stdout, "%s: %s v%s (compiled: " __DATE__ ", " __TIME__ " for %s)\n",
				argv[0], PROGNAME, VERSION, ARCH);

	if (help > 0)
		write_log(stdout, "usage: %s [-h] [-v] [-V]\n", argv[0]);

	if (version > 0 || help > 0)
		return EXIT_SUCCESS;

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

	/* Probing for static pacserve and pacdbserve hosts takes some time.
	 * Receiving a SIGHUP at this time could kill us. So register signal
	 * SIGHUP here before probing. */
	signal(SIGHUP, sighup_callback);

	/* parse config file */
	if ((ini = iniparser_load(CONFIGFILE)) == NULL) {
		write_log(stderr, "cannot parse file " CONFIGFILE ", continue anyway\n");
		/* continue anyway, there is nothing essential in the config file */
	} else {
		/* get max threads */
		max_threads = iniparser_getint(ini, "general:max threads", max_threads);
		if (verbose > 0 && max_threads > 0)
			write_log(stdout, "Limiting number of threads to a maximum of %d\n", max_threads);

		/* store interfaces to ignore */
		if ((inistring = iniparser_getstring(ini, "general:ignore interfaces", NULL)) != NULL) {
			values = strdup(inistring);
			tmp_ignore_interfaces = ignore_interfaces;

			value = strtok(values, DELIMITER);
			while (value != NULL) {
				if (verbose > 0)
					write_log(stdout, "Ignoring interface: %s\n", value);
				tmp_ignore_interfaces->interface = strdup(value);
				tmp_ignore_interfaces->next = malloc(sizeof(struct ignore_interfaces));
				tmp_ignore_interfaces = tmp_ignore_interfaces->next;
				value = strtok(NULL, DELIMITER);
			}
			tmp_ignore_interfaces->interface = NULL;
			tmp_ignore_interfaces->next = NULL;
			free(values);
		}

		/* add static pacserve hosts */
		if ((inistring = iniparser_getstring(ini, "general:pacserve hosts", NULL)) != NULL) {
			values = strdup(inistring);
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				if (verbose > 0)
					write_log(stdout, "Adding static pacserve host: %s\n", value);

				if (strchr(value, ':') != NULL) {
					port = atoi(strchr(value, ':') + 1);
					*strchr(value, ':') = 0;
				} else
					port = PORT_PACSERVE;
				add_host(value, port, PACSERVE);
				value = strtok(NULL, DELIMITER);
			}
			free(values);
		}

		/* add static pacdbserve hosts */
		if ((inistring = iniparser_getstring(ini, "general:pacdbserve hosts", NULL)) != NULL) {
			values = strdup(inistring);
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				if (verbose > 0)
					write_log(stdout, "Adding static pacdbserve host: %s\n", value);

				if (strchr(value, ':') != NULL) {
					port = atoi(strchr(value, ':') + 1);
					*strchr(value, ':') = 0;
				} else
					port = PORT_PACDBSERVE;
				add_host(value, port, PACDBSERVE);
				value = strtok(NULL, DELIMITER);
			}
			free(values);
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

	/* register SIG{TERM,KILL,INT} signal callbacks */
	signal(SIGTERM, sig_callback);
	signal(SIGKILL, sig_callback);
	signal(SIGINT, sig_callback);

	/* report ready to systemd */
	sd_notify(0, "READY=1\nSTATUS=Waiting for requests to redirect...");

	/* run the main loop */
	avahi_simple_poll_loop(simple_poll);

	/* report stopping to systemd */
	sd_notify(0, "STOPPING=1\nSTATUS=Stopping...");

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

	sd_notify(0, "STATUS=Stopped. Bye!");

	return ret;
}

// vim: set syntax=c:
