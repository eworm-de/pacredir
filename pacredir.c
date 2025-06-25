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
unsigned int count_redirect = 0, count_not_found = 0;

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
char * get_url(const char * hostname, AvahiProtocol proto, const char * address, const uint16_t port, const uint8_t dbfile, const char * uri) {
	const char * host, * dir;
	char * url;

	host = *address ? address : hostname;

	dir = dbfile ? "db" : "pkg";

	url = malloc(10 /* static chars of an url & null char */
			+ strlen(host)
			+ 5 /* max strlen of decimal 16bit value */
			+ 2 /* square brackets for IPv6 address */
			+ 4 /* extra dir */
			+ strlen(uri));

	if (*address != 0 && proto == AVAHI_PROTO_INET6)
		sprintf(url, "http://[%s]:%d/%s/%s", address, port, dir, uri);
	else
		sprintf(url, "http://%s:%d/%s/%s", host, port, dir, uri);

	return url;
}

/*** add_host ***/
int add_host(const char * host, AvahiProtocol proto, const char * address, const uint16_t port, const char * type) {
	struct hosts * tmphosts = hosts;
	struct request request;

	while (tmphosts->host != NULL) {
		if (strcmp(tmphosts->host, host) == 0 && tmphosts->proto == proto) {
			/* host already exists */
			if (verbose > 0)
				write_log(stdout, "Updating service %s (port %d) on host %s (%s)\n",
						type, port, host, avahi_proto_to_string(proto));
			goto update;
		}
		tmphosts = tmphosts->next;
	}

	/* host not found, adding a new one */
	if (verbose > 0)
		write_log(stdout, "Adding host %s (%s) with service %s (port %d)\n",
				host, avahi_proto_to_string(proto), type, port);

	tmphosts->host = strdup(host);
	tmphosts->proto = AVAHI_PROTO_UNSPEC;
	*tmphosts->address = 0;

	tmphosts->port = 0;
	tmphosts->online = 0;
	tmphosts->badtime = 0;
	tmphosts->badcount = 0;

	tmphosts->next = malloc(sizeof(struct hosts));
	tmphosts->next->host = NULL;
	tmphosts->next->next = NULL;

update:
	tmphosts->proto = proto;
	if (address != NULL)
		memcpy(tmphosts->address, address, AVAHI_ADDRESS_STR_MAX);

	tmphosts->online = 1;
	tmphosts->port = port;

	/* do a first request and let get_http_code() set the bad status */
	request.host = tmphosts;
	request.url = get_url(request.host->host, request.host->proto, request.host->address, request.host->port, 0, "");
	request.http_code = 0;
	request.last_modified = 0;
	get_http_code(&request);
	free(request.url);

	return EXIT_SUCCESS;
}

/*** remove_host ***/
int remove_host(const char * host, AvahiProtocol proto, const char * type) {
	struct hosts * tmphosts = hosts;

	while (tmphosts->host != NULL) {
		if (strcmp(tmphosts->host, host) == 0 && tmphosts->proto == proto) {
			if (verbose > 0)
				write_log(stdout, "Marking service %s on host %s (%s) offline\n",
						type, host, avahi_proto_to_string(proto));
			tmphosts->online = 0;
			break;
		}
		tmphosts = tmphosts->next;
	}

	return EXIT_SUCCESS;
}

/*** resolve_callback ***
 * Called whenever a service has been resolved successfully or timed out */
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
		void* userdata) {
	char ipaddress[AVAHI_ADDRESS_STR_MAX];
	char intname[IFNAMSIZ];

	assert(r);

	if_indextoname(interface, intname);

	switch (event) {
		case AVAHI_RESOLVER_FAILURE:
			write_log(stderr, "Failed to resolve service '%s' of type '%s' in domain '%s': %s\n",
					name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
			break;

		case AVAHI_RESOLVER_FOUND:
			avahi_address_snprint(ipaddress, AVAHI_ADDRESS_STR_MAX, address);

			if (verbose > 0)
				write_log(stdout, "Found service %s on host %s (%s) on interface %s\n",
						type, host, ipaddress, intname);

			add_host(host, protocol, ipaddress, port, type);
			break;
	}

	avahi_service_resolver_free(r);
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
		AvahiLookupResultFlags flags,
		void* userdata) {
	char * host;
	char intname[IFNAMSIZ];
	struct ignore_interfaces * tmp_ignore_interfaces = ignore_interfaces;
	AvahiClient * c;

	assert(b);

	c = userdata;
	if_indextoname(interface, intname);

	switch (event) {
		case AVAHI_BROWSER_FAILURE:
			write_log(stderr, "Failed to browse: %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
			avahi_simple_poll_quit(simple_poll);
			return;

		case AVAHI_BROWSER_NEW:
			host = get_fqdn(name, domain);

			if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
				goto out;

			/* check whether to ignore the interface */
			while (tmp_ignore_interfaces->next != NULL) {
				if (strcmp(intname, tmp_ignore_interfaces->interface) == 0) {
					if (verbose > 0)
						write_log(stdout, "Ignoring service %s on host %s on interface %s\n",
								type, host, intname);
					goto out;
				}
				tmp_ignore_interfaces = tmp_ignore_interfaces->next;
			}

			if ((avahi_service_resolver_new(c, interface, protocol, name, type, domain, protocol, 0, resolve_callback, c)) == NULL)
				write_log(stderr, "Failed to create resolver for service '%s' of type '%s' in domain '%s': %s\n",
						name, type, domain, avahi_strerror(avahi_client_errno(c)));
out:
			free(host);

			break;

		case AVAHI_BROWSER_REMOVE:
			host = get_fqdn(name, domain);

			if (verbose > 0)
				write_log(stdout, "Service %s on host %s disappeared\n",
						type, host);

			remove_host(host, protocol, type);

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
		void * userdata) {
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
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "pacredir/" VERSION " (" ID "/" ARCH ")");
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
			write_log(stderr, "Could not connect to peer %s on port %d: %s\n",
					request->host->host, request->host->port,
					*errbuf != 0 ? errbuf : curl_easy_strerror(res));
			request->http_code = 0;
			request->last_modified = 0;
			request->host->badtime = tv.tv_sec;
			request->host->badcount++;
			return NULL;
		} else {
			request->host->badtime = 0;
			request->host->badcount = 0;
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
static mhd_result ahc_echo(void * cls,
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

	char * url = NULL, * page = NULL;
	const char * basename, * host = NULL;
	struct timeval tv;

	struct tm tm;
	const char * if_modified_since = NULL;
	time_t last_modified = 0;
	uint8_t dbfile = 0;
	int i, error, req_count = -1;
	pthread_t * tid = NULL;
	struct request ** requests = NULL;
	struct request * request = NULL;
	long http_code = 0;
	double time_total = INFINITY;
	char ctime[26];

	/* initialize struct timeval */
	gettimeofday(&tv, NULL);

	/* we want the filename, not the path */
	basename = uri;
	while (strstr(basename, "/") != NULL)
		basename = strstr(basename, "/") + 1;

	/* unexpected method */
	if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0)
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
		host = basename = "project site";
		goto response;
	}

	/* process db file request (*.db and *.files) */
	if ((strlen(basename) > 3 && strcmp(basename + strlen(basename) - 3, ".db") == 0) ||
			(strlen(basename) > 6 && strcmp(basename + strlen(basename) - 6, ".files") == 0)) {

		dbfile = 1;

		/* get timestamp from request */
		if ((if_modified_since = MHD_lookup_connection_value(connection,
				MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_MODIFIED_SINCE))) {
			if (strptime(if_modified_since, "%a, %d %b %Y %H:%M:%S %Z", &tm) != NULL) {
				last_modified = timegm(&tm);
			}
		}
	}

	/* try to find a peer with most recent file */
	while (tmphosts->host != NULL) {
		struct hosts * host = tmphosts;
		time_t badtime = host->badtime + host->badcount * BADTIME;

		/* skip host if offline or had a bad request within last BADTIME seconds */
		if (host->online == 0) {
			if (verbose > 0)
				write_log(stdout, "Service %s on host %s is offline, skipping\n",
						PACSERVE, tmphosts->host);
			tmphosts = tmphosts->next;
			continue;
		} else if (badtime > tv.tv_sec) {
			if (verbose > 0) {
				/* write the time to buffer ctime, then strip the line break */
				ctime_r(&badtime, ctime);
				ctime[strlen(ctime) - 1] = '\0';

				write_log(stdout, "Service %s on host %s is marked bad until %s, skipping\n",
						PACSERVE, tmphosts->host, ctime);
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
		request->host = tmphosts;
		request->url = get_url(request->host->host, request->host->proto, request->host->address, request->host->port, dbfile, basename);
		request->http_code = 0;
		request->last_modified = 0;

		if (verbose > 0)
			write_log(stdout, "Trying %s: %s\n", request->host, request->url);

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
				/* for db files choose the most recent peer when not too old */
				((dbfile == 1 && ((request->last_modified > last_modified &&
						   request->last_modified + 86400 > time(NULL)) ||
				/* but use a faster peer if available */
						  (url != NULL &&
						   request->last_modified >= last_modified &&
						   request->time_total < time_total))) ||
				 /* for packages try to guess the fastest peer */
				 (dbfile == 0 && request->time_total < time_total))) {
			if (url != NULL)
				free(url);
			url = request->url;
			host = request->host->host;
			http_code = MHD_HTTP_OK;
			last_modified = request->last_modified;
			time_total = request->time_total;
		} else
			free(request->url);
		free(request);
	}

	/* increase counters before reponse label,
	   do not count redirects to project page */
	if (http_code == MHD_HTTP_OK)
		count_redirect++;
	else
		count_not_found++;

response:
	/* give response */
	if (http_code == MHD_HTTP_OK) {
		write_log(stdout, "Redirecting to %s: %s\n", host, url);
		page = malloc(strlen(PAGE307) + strlen(url) + strlen(basename) + 1);
		sprintf(page, PAGE307, url, basename);
		response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_MUST_FREE);
		ret = MHD_add_response_header(response, "Location", url);
		ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, response);
		free(url);
	} else {
		if (req_count < 0)
			write_log(stdout, "Currently no peers are available to check for %s.\n",
					basename);
		else if (dbfile > 0)
			write_log(stdout, "No more recent version of %s found on %d peers.\n",
					basename, req_count + 1);
		else
			write_log(stdout, "File %s not found on %d peers, giving up.\n",
					basename, req_count + 1);

		page = malloc(strlen(PAGE404) + strlen(basename) + 1);
		sprintf(page, PAGE404, basename);
		response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_MUST_FREE);
		ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
	}

	MHD_destroy_response(response);

	/* report counts to systemd */
	sd_notifyf(0, "STATUS=%d redirects, %d not found, waiting...",
			count_redirect, count_not_found);

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
		tmphosts->badtime = 0;
		tmphosts->badcount = 0;
		tmphosts = tmphosts->next;
	}
}

/*** main ***/
int main(int argc, char ** argv) {
	dictionary * ini;
	const char * inistring;
	char * values, * value;
	int8_t use_proto = AVAHI_PROTO_UNSPEC;
	uint16_t port;
	struct ignore_interfaces * tmp_ignore_interfaces;
	AvahiClient *client = NULL;
	AvahiServiceBrowser *pacserve = NULL;
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
		write_log(stdout, "%s: " PROGNAME " v" VERSION " " ID "/" ARCH
#if REPRODUCIBLE == 0
				" (compiled: " __DATE__ ", " __TIME__ ")"
#endif
				"\n", argv[0]);

	if (help > 0)
		write_log(stdout, "usage: %s [-h] [-v] [-V]\n", argv[0]);

	if (version > 0 || help > 0)
		return EXIT_SUCCESS;

	if (getuid() == 0) {
		/* process is running as root, drop privileges */
		if (verbose > 0)
			write_log(stdout, "Running as root, meh! Dropping privileges.\n");
		if (setgid(DROP_PRIV_GID) != 0 || setuid(DROP_PRIV_UID) != 0)
			write_log(stderr, "Unable to drop user privileges!\n");
	}

	/* allocate first struct element as dummy */
	hosts = malloc(sizeof(struct hosts));
	hosts->host = NULL;
	hosts->online = 0;
	hosts->badtime = 0;
	hosts->next = NULL;

	ignore_interfaces = malloc(sizeof(struct ignore_interfaces));
	ignore_interfaces->interface = NULL;
	ignore_interfaces->next = NULL;

	/* Probing for static pacserve hosts takes some time.
	 * Receiving a SIGHUP at this time could kill us. So register signal
	 * SIGHUP here before probing. */
	struct sigaction act_hup = { 0 };
	act_hup.sa_handler = sighup_callback;
	sigaction(SIGHUP, &act_hup, NULL);

	/* parse config file */
	if ((ini = iniparser_load(CONFIGFILE)) == NULL) {
		write_log(stderr, "cannot parse file " CONFIGFILE ", continue anyway\n");
		/* continue anyway, there is nothing essential in the config file */
	} else {
		int ini_verbose;
		const char * tmp;

		/* extra verbosity from config */
		ini_verbose = iniparser_getint(ini, "general:verbose", 0);
		verbose += ini_verbose;

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

		/* configure protocols to use */
		if ((tmp = iniparser_getstring(ini, "general:protocol", NULL)) != NULL) {
			switch(tmp[strlen(tmp) - 1]) {
				case '4':
					if (verbose > 0)
						write_log(stdout, "Using IPv4 only\n");
					use_proto = AVAHI_PROTO_INET;
					break;
				case '6':
					if (verbose > 0)
						write_log(stdout, "Using IPv6 only\n");
					use_proto = AVAHI_PROTO_INET6;
					break;
			}
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
				add_host(value, AVAHI_PROTO_UNSPEC, NULL, port, PACSERVE);
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
	if ((pacserve = avahi_service_browser_new(client, AVAHI_IF_UNSPEC,
			 use_proto, PACSERVE, NULL, 0, browse_callback, client)) == NULL) {
		write_log(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
		goto fail;
	}

	/* prepare struct to make microhttpd listen on localhost only */
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT_PACREDIR);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/* start http server */
	if ((mhd = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TCP_FASTOPEN, PORT_PACREDIR,
			NULL, NULL, &ahc_echo, NULL, MHD_OPTION_SOCK_ADDR, &address, MHD_OPTION_END)) == NULL) {
		write_log(stderr, "Could not start daemon on port %d.\n", PORT_PACREDIR);
		goto fail;
	}

	/* initialize curl */
	curl_global_init(CURL_GLOBAL_ALL);

	/* register SIG{TERM,KILL,INT} signal callbacks */
	struct sigaction act = { 0 };
	act.sa_handler = sig_callback;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGKILL, &act, NULL);
	sigaction(SIGINT, &act, NULL);

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
