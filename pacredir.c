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
uint8_t quit = 0, verbose = 0;
unsigned int count_redirect = 0, count_not_found = 0;

/*** write_log ***/
static int write_log(FILE *stream, const char *format, ...) {
	va_list args;
	va_start(args, format);

	vfprintf(stream, format, args);
	fflush(stream);

	return EXIT_SUCCESS;
}

/*** get_url ***/
static char * get_url(const char * hostname, const uint16_t port, const uint8_t dbfile, const char * uri) {
	const char * dir;
	char * url;

	dir = dbfile ? "db" : "pkg";
	url = malloc(11 /* static chars of an url & null char */
			+ strlen(hostname)
			+ 5 /* max strlen of decimal 16bit value */
			+ strlen(dir)
			+ strlen(uri));

	sprintf(url, "http://%s:%d/%s/%s", hostname, port, dir, uri);

	return url;
}

/*** update_interfaces ***/
static void update_interfaces(void) {
	struct ignore_interfaces *ignore_interfaces_ptr = ignore_interfaces;

	while (ignore_interfaces_ptr->interface != NULL) {
		ignore_interfaces_ptr->ifindex = if_nametoindex(ignore_interfaces_ptr->interface);
		ignore_interfaces_ptr = ignore_interfaces_ptr->next;
	}
}

/*** get_name ***/
static size_t get_name(const uint8_t* rr_ptr, char* name) {
	uint8_t dot = 0;
	char *name_ptr = name;

	for (;;) {
		if (*rr_ptr == 0)
			return (strlen(name) + 2);
		if (dot)
			*(name_ptr++) = '.';
		else
			dot++;
		memcpy(name_ptr, rr_ptr + 1, *rr_ptr + 1);
		name_ptr += *rr_ptr;
		rr_ptr += *rr_ptr + 1;
	}
}

/*** process_reply_record ***/
static char* process_reply_record(const void *rr, size_t sz) {
	uint16_t class, type, rdlength;
	const uint8_t *rr_ptr = rr;
	char *name;
	uint32_t ttl;

	rr_ptr += strlen((char*)rr_ptr) + 1;
	memcpy(&type, rr_ptr, sizeof(uint16_t));
	rr_ptr += sizeof(uint16_t);
	memcpy(&class, rr_ptr, sizeof(uint16_t));
	rr_ptr += sizeof(uint16_t);
	memcpy(&ttl, rr_ptr, sizeof(uint32_t));
	rr_ptr += sizeof(uint32_t);
	memcpy(&rdlength, rr_ptr, sizeof(uint16_t));
	rr_ptr += sizeof(uint16_t);
	assert(be16toh(type) == DNS_TYPE_PTR);
	assert(be16toh(class) == DNS_CLASS_IN);

	name = malloc(strlen((char*)rr_ptr) + 1);
	rr_ptr += get_name(rr_ptr, name);

	assert(rr_ptr == (const uint8_t*) rr + sz);

	return name;
}

/*** update_hosts ***/
static void update_hosts(void) {
	struct hosts *hosts_ptr = hosts;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply_record = NULL;
	sd_bus *bus = NULL;
	uint64_t flags;
	int r;

	/* set 'present' to 0, so we later know which hosts were available, and which were not */
	while (hosts_ptr->host != NULL) {
		hosts_ptr->present = 0;
		hosts_ptr = hosts_ptr->next;
	}

	r = sd_bus_open_system(&bus);
	if (r < 0) {
		write_log(stderr, "Failed to open system bus: %s\n", strerror(-r));
		goto finish;
	}

	r = sd_bus_call_method(bus, "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
		"org.freedesktop.resolve1.Manager", "ResolveRecord", &error,
		&reply_record, "isqqt", 0 /* any */, PACSERVE "." MDNS_DOMAIN,
		DNS_CLASS_IN, DNS_TYPE_PTR, SD_RESOLVED_NO_SYNTHESIZE|SD_RESOLVED_NO_ZONE);
	if (r < 0) {
		if (verbose > 0)
			write_log(stderr, "Failed to resolve record: %s\n", error.message);
		sd_bus_error_free(&error);
		goto finish;
	}

	r = sd_bus_message_enter_container(reply_record, 'a', "(iqqay)");
	if (r < 0)
		goto parse_failure_record;

	for (;;) {
		int ifindex;
		uint16_t class, type, port;
		const void *data;
		size_t length;
		const char *canonical, *discard;
		uint8_t match = 0, ignore = 0;

		r = sd_bus_message_enter_container(reply_record, 'r', "iqqay");
		if (r < 0)
			goto parse_failure_record;
		if (r == 0)  /* Reached end of array */
			break;
		r = sd_bus_message_read(reply_record, "iqq", &ifindex, &class, &type);
		if (r < 0)
			goto parse_failure_record;
		r = sd_bus_message_read_array(reply_record, 'y', &data, &length);
		if (r < 0)
			goto parse_failure_record;
		r = sd_bus_message_exit_container(reply_record);
		if (r < 0)
			goto parse_failure_record;

		/* process the data received */
		char *peer = process_reply_record(data, length);

		sd_bus_message *reply_service = NULL;
		/* service START */
		r = sd_bus_call_method(bus, "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
			"org.freedesktop.resolve1.Manager", "ResolveService", &error,
			&reply_service, "isssit", 0 /* any */, "", "", peer, AF_UNSPEC, UINT64_C(0));
		if (r < 0) {
			if (verbose > 0)
				write_log(stderr, "Failed to resolve service '%s': %s\n", peer, error.message);
			sd_bus_error_free(&error);
			goto finish_service;
		}

		r = sd_bus_message_enter_container(reply_service, 'a', "(qqqsa(iiay)s)");
		if (r < 0)
			goto parse_failure_service;

		for (;;) {
			uint16_t priority, weight;
			const char *hostname;

			r = sd_bus_message_enter_container(reply_service, 'r', "qqqsa(iiay)s");
			if (r < 0)
				goto parse_failure_service;
			if (r == 0)  /* Reached end of array */
				break;
			r = sd_bus_message_read(reply_service, "qqqs", &priority, &weight, &port, &hostname);
			if (r < 0)
				goto parse_failure_service;

			r = sd_bus_message_enter_container(reply_service, 'a', "(iiay)");
			if (r < 0)
				goto parse_failure_service;

			for (;;) {
				int ifindex, family;
				const void *data;
				size_t length;
				struct ignore_interfaces *ignore_interfaces_ptr = ignore_interfaces;

				r = sd_bus_message_enter_container(reply_service, 'r', "iiay");
				if (r < 0)
					goto parse_failure_service;
				if (r == 0)  /* Reached end of array */
					break;
				r = sd_bus_message_read(reply_service, "ii", &ifindex, &family);
				if (r < 0)
					goto parse_failure_service;
				r = sd_bus_message_read_array(reply_service, 'y', &data, &length);
				if (r < 0)
					goto parse_failure_service;
				r = sd_bus_message_exit_container(reply_service);
				if (r < 0)
					goto parse_failure_service;

				while (ignore_interfaces_ptr->interface != NULL) {
					if (ignore_interfaces_ptr->ifindex == ifindex) {
						ignore++;
						break;
					}
					ignore_interfaces_ptr = ignore_interfaces_ptr->next;
				}
			}
			r = sd_bus_message_exit_container(reply_service);
			if (r < 0)
				goto parse_failure_service;

			r = sd_bus_message_read(reply_service, "s", &canonical);
			if (r < 0)
				goto parse_failure_service;
			r = sd_bus_message_exit_container(reply_service);
			if (r < 0)
				goto parse_failure_service;
		}

		r = sd_bus_message_exit_container(reply_service);
		if (r < 0)
			goto parse_failure_service;
		r = sd_bus_message_enter_container(reply_service, 'a', "ay");
		if (r < 0)
			goto parse_failure_service;

		for(;;) {
			const void *txt_data;
			size_t txt_len;

			r = sd_bus_message_read_array(reply_service, 'y', &txt_data, &txt_len);
			if (r < 0)
				goto parse_failure_service;
			if (r == 0)  /* Reached end of array */
				break;

			/* does the TXT data match our architecture (arch) or distribution (id)? */
			if (strncmp((char*)txt_data, "arch=" ARCH, txt_len) == 0)
				match |= DNS_SRV_TXT_MATCH_ARCH;
			if (strncmp((char*)txt_data, "id=" ID, txt_len) == 0)
				match |= DNS_SRV_TXT_MATCH_ID;
		}

		r = sd_bus_message_exit_container(reply_service);
		if (r < 0)
			goto parse_failure_service;

		r = sd_bus_message_read(reply_service, "s", &discard);
		if (r < 0)
			goto parse_failure_service;
		r = sd_bus_message_read(reply_service, "s", &discard);
		if (r < 0)
			goto parse_failure_service;
		r = sd_bus_message_read(reply_service, "s", &discard);
		if (r < 0)
			goto parse_failure_service;

		r = sd_bus_message_read(reply_service, "t", &flags);
		if (r < 0)
			goto parse_failure_service;

		if (ignore > 0) {
			if (verbose > 0)
				write_log(stdout, "Host %s is on an ignored interface.\n", canonical);
			goto finish_service;
		}

		if (match < DNS_SRV_TXT_MATCH_ALL) {
			if (verbose > 0)
				write_log(stdout, "Host %s does not match distribution and/or architecture.\n", canonical);
			goto finish_service;
		}

		/* add the peer to our struct */
		add_host(canonical, port, 1);

		goto finish_service;

parse_failure_service:
		write_log(stderr, "Parse failure for service: %s\n", strerror(-r));

finish_service:
		sd_bus_message_unref(reply_service);
	}

	r = sd_bus_message_exit_container(reply_record);
	if (r < 0)
		goto parse_failure_record;
	r = sd_bus_message_read(reply_record, "t", &flags);
	if (r < 0)
		goto parse_failure_record;

	/* mark hosts offline that did not show up in query */
	hosts_ptr = hosts;
	while (hosts_ptr->host != NULL) {
		if (hosts_ptr->mdns == 1 && hosts_ptr->online == 1 && hosts_ptr->present == 0) {
			if (verbose > 0)
				write_log(stdout, "Marking host %s offline\n", hosts_ptr->host);
			hosts_ptr->online = 0;
		}
		hosts_ptr = hosts_ptr->next;
	}

	goto finish;

parse_failure_record:
	write_log(stderr, "Parse failure for record: %s\n", strerror(-r));

finish:
	sd_bus_message_unref(reply_record);
	sd_bus_flush_close_unref(bus);
}

/*** add_host ***/
static int add_host(const char * host, const uint16_t port, const uint8_t mdns) {
	struct hosts * hosts_ptr = hosts;

	while (hosts_ptr->host != NULL) {
		if (strcmp(hosts_ptr->host, host) == 0) {
			/* host already exists */
			if (verbose > 0)
				write_log(stdout, "Updating host %s with port %d\n",
						host, port);
			goto update;
		}
		hosts_ptr = hosts_ptr->next;
	}

	/* host not found, adding a new one */
	if (verbose > 0)
		write_log(stdout, "Adding host %s with port %d\n",
				host, port);

	hosts_ptr->host = strdup(host);
	hosts_ptr->mdns = mdns;
	hosts_ptr->badtime = 0;
	hosts_ptr->badcount = 0;

	hosts_ptr->next = malloc(sizeof(struct hosts));
	hosts_ptr->next->host = NULL;
	hosts_ptr->next->next = NULL;

update:
	hosts_ptr->port = port;
	hosts_ptr->online = 1;
	hosts_ptr->present = 1;

	return EXIT_SUCCESS;
}

/*** remove_host ***/
/* currently unused, but could become important if continuous
   mDNS querying becomes available again on day...
static int remove_host(const char * host) {
	struct hosts * hosts_ptr = hosts;

	while (hosts_ptr->host != NULL) {
		if (strcmp(hosts_ptr->host, host) == 0) {
			if (verbose > 0)
				write_log(stdout, "Marking host %s offline\n", host);
			hosts_ptr->online = 0;
			break;
		}
		hosts_ptr = hosts_ptr->next;
	}

	return EXIT_SUCCESS;
} */

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
	struct hosts * hosts_ptr = hosts;

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
	while (hosts_ptr->host != NULL) {
		time_t badtime = hosts_ptr->badtime + hosts_ptr->badcount * BADTIME;

		/* skip host if offline or had a bad request within last BADTIME seconds */
		if (hosts_ptr->online == 0) {
			if (verbose > 0)
				write_log(stdout, "Host %s is offline, skipping\n",
						hosts_ptr->host);
			hosts_ptr = hosts_ptr->next;
			continue;
		} else if (badtime > tv.tv_sec) {
			if (verbose > 0) {
				/* write the time to buffer ctime, then strip the line break */
				ctime_r(&badtime, ctime);
				ctime[strlen(ctime) - 1] = '\0';

				write_log(stdout, "Host %s is marked bad until %s, skipping\n",
						hosts_ptr->host, ctime);
			}
			hosts_ptr = hosts_ptr->next;
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
		request->host = hosts_ptr;
		request->url = get_url(request->host->host, request->host->port, dbfile, basename);
		request->http_code = 0;
		request->last_modified = 0;

		if (verbose > 0)
			write_log(stdout, "Trying %s: %s\n", request->host->host, request->url);

		if ((error = pthread_create(&tid[req_count], NULL, get_http_code, (void *)request)) != 0)
			write_log(stderr, "Could not run thread number %d, errno %d\n", req_count, error);

		hosts_ptr = hosts_ptr->next;
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
static void sig_callback(int signal) {
	write_log(stdout, "Received signal '%s', quitting.\n", strsignal(signal));

	quit++;
}

/*** sighup_callback ***/
static void sighup_callback(int signal) {
	struct hosts * hosts_ptr = hosts;

	write_log(stdout, "Received signal '%s', resetting bad counts, updating interfaces and hosts.\n",
		strsignal(signal));

	while (hosts_ptr->host != NULL) {
		hosts_ptr->badtime = 0;
		hosts_ptr->badcount = 0;
		hosts_ptr = hosts_ptr->next;
	}
}

/*** sigusr_callback ***/
static void sigusr_callback(int signal) {
	struct ignore_interfaces * ignore_interfaces_ptr = ignore_interfaces;
	struct hosts * hosts_ptr = hosts;

	write_log(stdout, "Received signal '%s', dumping state.\n", strsignal(signal));

	write_log(stdout, "Ignored interfaces:\n");
	while (ignore_interfaces_ptr->interface != NULL) {
		if (ignore_interfaces_ptr->ifindex > 0)
			write_log(stdout, " -> %s (link %d)\n",
				ignore_interfaces_ptr->interface,  ignore_interfaces_ptr->ifindex);
		else
			write_log(stdout, " -> %s (N/A)\n", ignore_interfaces_ptr->interface);

		ignore_interfaces_ptr = ignore_interfaces_ptr->next;
	}

	write_log(stdout, "Known hosts:\n");
	while (hosts_ptr->host != NULL) {
		if (hosts_ptr->badcount > 0)
			write_log(stdout, " -> %s (%s, %s, bad count: %d)\n",
				hosts_ptr->host, hosts_ptr->mdns ? "mdns" : "static",
				hosts_ptr->online ? "online" : "offline", hosts_ptr->badcount);
		else
			write_log(stdout, " -> %s (%s, %s)\n",
				hosts_ptr->host, hosts_ptr->mdns ? "mdns" : "static",
				hosts_ptr->online ? "online" : "offline");

		hosts_ptr = hosts_ptr->next;
	}

	write_log(stdout, "%d redirects, %d not found.\n",
		count_redirect, count_not_found);
}

/*** main ***/
int main(int argc, char ** argv) {
	dictionary * ini;
	const char * inistring;
	char * values, * value;
	uint16_t port;
	struct ignore_interfaces * ignore_interfaces_ptr;
	int i, ret = 1;
	struct MHD_Daemon * mhd;
	struct hosts * hosts_ptr;
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
	ignore_interfaces->ifindex = 0;
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
			ignore_interfaces_ptr = ignore_interfaces;

			value = strtok(values, DELIMITER);
			while (value != NULL) {
				if (verbose > 0)
					write_log(stdout, "Ignoring interface: %s\n", value);
				ignore_interfaces_ptr->interface = strdup(value);
				ignore_interfaces_ptr->next = malloc(sizeof(struct ignore_interfaces));
				ignore_interfaces_ptr = ignore_interfaces_ptr->next;
				value = strtok(NULL, DELIMITER);
			}
			ignore_interfaces_ptr->interface = NULL;
			ignore_interfaces_ptr->next = NULL;
			free(values);
		}

		/* add static pacserve hosts */
		if ((inistring = iniparser_getstring(ini, "general:pacserve hosts", NULL)) != NULL) {
			values = strdup(inistring);
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				if (verbose > 0)
					write_log(stdout, "Adding static host: %s\n", value);

				if (strchr(value, ':') != NULL) {
					port = atoi(strchr(value, ':') + 1);
					*strchr(value, ':') = 0;
				} else
					port = PORT_PACSERVE;
				add_host(value, port, 0);
				value = strtok(NULL, DELIMITER);
			}
			free(values);
		}

		/* done reading config file, free */
		iniparser_freedict(ini);
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

	if (verbose > 0)
		write_log(stdout, "Listening on port %d\n", PORT_PACREDIR);

	/* initialize curl */
	curl_global_init(CURL_GLOBAL_ALL);

	/* register SIG{INT,KILL,TERM} signal callbacks */
	struct sigaction act = { 0 };
	act.sa_handler = sig_callback;
	sigaction(SIGINT,  &act, NULL);
	sigaction(SIGKILL, &act, NULL);
	sigaction(SIGTERM, &act, NULL);

	/* register SIGUSR[12] signal callbacks */
	struct sigaction act_usr = { 0 };
	act_usr.sa_handler = sigusr_callback;
	sigaction(SIGUSR1, &act_usr, NULL);
	sigaction(SIGUSR2, &act_usr, NULL);

	/* report ready to systemd */
	sd_notify(0, "READY=1\nSTATUS=Waiting for requests to redirect...");

	/* main loop */
	while (quit == 0) {
		update_interfaces();
		update_hosts();
		sleep(60);
	}

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
		hosts_ptr = hosts->next;
		free(hosts);
		hosts = hosts_ptr;
	}

	while (ignore_interfaces->interface != NULL) {
		free(ignore_interfaces->interface);
		ignore_interfaces_ptr = ignore_interfaces->next;
		free(ignore_interfaces);
		ignore_interfaces = ignore_interfaces_ptr;
	}

	sd_notify(0, "STATUS=Stopped. Bye!");

	return ret;
}

// vim: set syntax=c:
