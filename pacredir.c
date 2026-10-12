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
struct interfaces * interfaces = NULL;
uint8_t quit = 0, systemd = 0, update = 0, verbose = 0;
unsigned int count_redirect = 0, count_not_found = 0;

/* include more code */
#include "pacredir-hosts.c"
#include "pacredir-interfaces.c"
#include "pacredir-request.c"

/*** write_log ***/
static int write_log(uint8_t show, uint8_t priority, const char *format, ...) {
	va_list args;
	char * message = NULL;
	size_t len = 0;
	FILE * stream;

	if (show == 0)
		return EXIT_SUCCESS;

	va_start(args, format);
	len = vsnprintf(NULL, 0, format, args) + 1;
	message = malloc(len);
	va_end(args);

	va_start(args, format);
	vsnprintf(message, len, format, args);
	va_end(args);

	/* send to systemd-journald */
	sd_journal_send("MESSAGE=%s", message,
		"SYSLOG_IDENTIFIER=" PROGNAME,
		"PRIORITY=%i", priority,
		NULL);

	if (systemd)
		goto free_return;

	switch (priority) {
		case LOG_WARNING:
		case LOG_ERR:
			stream = stderr;
			break;
		default:
			stream = stdout;
			break;
	}

	fprintf(stream, "%s\n", message);
	fflush(stream);

free_return:
	free(message);

	return EXIT_SUCCESS;
}

/* append_string */
static char * append_string(char * string, const char *format, ...) {
	va_list args;
	size_t string_len = 0, append_len;

	if (string != NULL)
		string_len = strlen(string);

	va_start(args, format);
	append_len = vsnprintf(NULL, 0, format, args) + 1;
	va_end(args);

	string = realloc(string, string_len + append_len);

	va_start(args, format);
	vsnprintf(string + string_len, append_len, format, args);
	va_end(args);

	return string;
}

/*** status_page ***/
static char * status_page(void) {
	struct interfaces * interfaces_ptr = interfaces;
	struct hosts * hosts_ptr = hosts;
	char *page = NULL, *overall = CIRCLE_BLUE;
	char hostname[HOST_NAME_MAX];
	struct timeval tv;

	/* initialize struct timeval */
	gettimeofday(&tv, NULL);

	if (count_redirect + count_not_found)
		switch (count_redirect * 4 / (count_redirect + count_not_found)) {
			case 0:
				overall = CIRCLE_RED;
				break;
			case 1:
				overall = CIRCLE_ORANGE;
				break;
			case 2:
				overall = CIRCLE_YELLOW;
				break;
			default:
				overall = CIRCLE_GREEN;
				break;
		}

	gethostname(hostname, HOST_NAME_MAX);
	page = append_string(page, STATUS_HEAD, hostname, count_redirect, count_not_found, overall);

	page = append_string(page, STATUS_INT_HEAD);
	if (interfaces_ptr->interface == NULL)
		page = append_string(page, STATUS_INT_NONE);
	while (interfaces_ptr->interface != NULL) {
		uint8_t not_avail = !interfaces_ptr->up || !interfaces_ptr->running || interfaces_ptr->ignore;
		const char * state = (interfaces_ptr->up ? (interfaces_ptr->running ? "running" : "up") : "down");

		if (interfaces_ptr->ifindex > 0)
			page = append_string(page, STATUS_INT_ONE,
				not_avail ? "offline" : "online", interfaces_ptr->interface,
				interfaces_ptr->ignore ? CIRCLE_PURPLE : CIRCLE_GREEN, interfaces_ptr->ignore ? "yes" : "no",
				interfaces_ptr->up ? (interfaces_ptr->running ? CIRCLE_GREEN : CIRCLE_ORANGE) : CIRCLE_RED,
				state, interfaces_ptr->ifindex);
		else
			page = append_string(page, STATUS_INT_ONE_NA,
				interfaces_ptr->interface,
				interfaces_ptr->ignore ? CIRCLE_PURPLE : CIRCLE_GREEN, interfaces_ptr->ignore ? "yes" : "no");

		interfaces_ptr = interfaces_ptr->next;
	}
	page = append_string(page, STATUS_INT_FOOT);

	page = append_string(page, STATUS_HOST_HEAD);
	if (hosts_ptr->host == NULL)
		page = append_string(page, STATUS_HOST_NONE);
	while (hosts_ptr->host != NULL) {
		time_t badtime = hosts_ptr->badtime + hosts_ptr->badcount * BADTIME;
		uint8_t bad = hosts_ptr->badcount && badtime > tv.tv_sec ? 1 : 0;

		const char *state = hosts_ptr->mdns ? (hosts_ptr->online ? "online" : "offline") : "static";
		page = append_string(page, STATUS_HOST_ONE,
			state, hosts_ptr->host, hosts_ptr->port,
			hosts_ptr->mdns ? (hosts_ptr->online ? CIRCLE_GREEN : CIRCLE_RED) : CIRCLE_BLUE,
			state, hosts_ptr->redir ? CIRCLE_GREEN : CIRCLE_BLUE, hosts_ptr->redir,
			hosts_ptr->finds ? CIRCLE_GREEN : CIRCLE_BLUE, hosts_ptr->finds,
			bad ? CIRCLE_RED : CIRCLE_BLUE, hosts_ptr->badcount);

		hosts_ptr = hosts_ptr->next;
	}
	page = append_string(page, STATUS_HOST_FOOT);

	page = append_string(page, STATUS_FOOT);

	return page;
}

/*** ahc_echo ***
 * called whenever a http request is received */
static enum MHD_Result ahc_echo(void * cls,
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

	struct request * request = NULL;
	char * page = NULL;
	static_file * file = NULL;
	const char * basename;
	struct timeval tv;

	struct tm tm;
	const char * if_modified_since = NULL;
	time_t last_modified = 0;
	uint8_t dbfile = 0;
	long http_code = MHD_HTTP_NOT_FOUND;

	/* initialize struct timeval */
	gettimeofday(&tv, NULL);

	/* give status page */
	if (strcmp(uri, "/") == 0) {
		http_code = MHD_HTTP_OK;
		page = status_page();
		goto response;
	}

	/* give a simple ok response for monitoring */
	if (strcmp(uri, "/check") == 0) {
		http_code = MHD_HTTP_OK;
		page = strdup("OK");
		goto response;
	}

	/* give favicon */
	if (strcmp(uri, "/favicon.png") == 0) {
		http_code = MHD_HTTP_OK;
		file = &favicon;
		goto response;
	}

	/* give style */
	if (strcmp(uri, "/style.css") == 0) {
		http_code = MHD_HTTP_OK;
		file = &style;
		goto response;
	}

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

	/* try to find the best redirect, and count */
	if ((request = find_best_redirect(basename, dbfile, last_modified)) != NULL) {
		http_code = MHD_HTTP_TEMPORARY_REDIRECT;
		count_redirect++;
	} else {
		count_not_found++;
	}

response:
	/* give response */
	if (http_code == MHD_HTTP_TEMPORARY_REDIRECT) {
		write_log(1, LOG_NOTICE, "Redirecting to %s: %s", request->host->host, request->url);
		request->host->redir++;
		page = malloc(strlen(PAGE307) + strlen(request->url) + strlen(basename) + 1);
		sprintf(page, PAGE307, request->url, basename);
		response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_MUST_FREE);
		ret = MHD_add_response_header(response, "Location", request->url);
		free(request);
	} else if (http_code == MHD_HTTP_OK) {
		if (page != NULL) {
			write_log(1, LOG_INFO, "Sending status page.");
			response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_MUST_FREE);
			ret = MHD_add_response_header(response, "Content-Type", "text/html");
		} else {
			write_log(1, LOG_INFO, "Sending static file: %s", uri);
			response = MHD_create_response_from_buffer(file->size, file->content, MHD_RESPMEM_PERSISTENT);
			ret = MHD_add_response_header(response, "ETag", file->sha1);
			ret = MHD_add_response_header(response, "Last-Modified", file->date);
			ret = MHD_add_response_header(response, "Content-Type", file->mime);
			ret = MHD_add_response_header(response, "Cache-Control", "max-age=86400");
		}
	} else { /* MHD_HTTP_NOT_FOUND */
		write_log(1, LOG_NOTICE, "Sending 'Not Found' for: %s", basename);
		page = malloc(strlen(PAGE404) + strlen(basename) + 1);
		sprintf(page, PAGE404, basename);
		response = MHD_create_response_from_buffer(strlen(page), (void*) page, MHD_RESPMEM_MUST_FREE);
	}

	ret = MHD_add_response_header(response, "Server", PROGNAME " v" VERSION " " ID "/" ARCH);
	ret = MHD_queue_response(connection, http_code, response);
	MHD_destroy_response(response);

	/* report counts to systemd */
	sd_notifyf(0, "STATUS=%d redirects, %d not found, waiting...",
			count_redirect, count_not_found);

	return ret;
}

/*** dump_state ***/
static void dump_state(void) {
	struct interfaces * interfaces_ptr = interfaces;
	struct hosts * hosts_ptr = hosts;
	struct timeval tv;

	/* initialize struct timeval */
	gettimeofday(&tv, NULL);

	write_log(1, LOG_INFO, "Interfaces:");
	if (interfaces_ptr->interface == NULL)
		write_log(1, LOG_INFO, " (none)");
	while (interfaces_ptr->interface != NULL) {
		uint8_t not_avail = !interfaces_ptr->up || !interfaces_ptr->running || interfaces_ptr->ignore;
		const char * state = (interfaces_ptr->up ? (interfaces_ptr->running ? "running" : "up") : "down");

		if (interfaces_ptr->ifindex > 0)
			write_log(1, LOG_INFO, " -> %s%s%s (ignore: %s, state: %s, link: %d)",
				not_avail ? "[" : "", interfaces_ptr->interface, not_avail ? "]" : "",
				interfaces_ptr->ignore ? "yes" : "no", state, interfaces_ptr->ifindex);
		else
			write_log(1, LOG_INFO, " -> %s%s%s (ignore: %s, state: %s, N/A)",
				not_avail ? "[" : "", interfaces_ptr->interface, not_avail ? "]" : "",
				interfaces_ptr->ignore ? "yes" : "no", state);

		interfaces_ptr = interfaces_ptr->next;
	}

	write_log(1, LOG_INFO, "Known hosts:");
	if (hosts_ptr->host == NULL)
		write_log(1, LOG_INFO, " (none)");
	while (hosts_ptr->host != NULL) {
		uint8_t not_avail = (hosts_ptr->mdns && !hosts_ptr->online) || (hosts_ptr->badcount &&
			(hosts_ptr->badtime + hosts_ptr->badcount * BADTIME) > tv.tv_sec) ? 1 : 0;

		write_log(1, LOG_INFO, " -> %s%s%s (%s, %s, port: %d, redir: %d, finds: %d, bad: %d)",
			not_avail ? "[" : "", hosts_ptr->host, not_avail ? "]" : "",
			hosts_ptr->mdns ? "mdns" : "static", hosts_ptr->online ? "online" : "offline",
			hosts_ptr->port, hosts_ptr->redir, hosts_ptr->finds, hosts_ptr->badcount);

		hosts_ptr = hosts_ptr->next;
	}

	write_log(1, LOG_NOTICE, "%d redirects, %d not found.",
		count_redirect, count_not_found);
}

/*** sig_callback ***/
static void sig_callback(int signal) {
	write_log(1, LOG_NOTICE, "Received signal '%s', quitting.", strsignal(signal));

	quit++;
}

/*** sighup_callback ***/
static void sighup_callback(int signal) {
	struct hosts * hosts_ptr = hosts;

	write_log(1, LOG_NOTICE, "Received signal '%s', resetting bad counts, updating interfaces and hosts.",
		strsignal(signal));

	while (hosts_ptr->host != NULL) {
		hosts_ptr->badtime = 0;
		hosts_ptr->badcount = 0;
		hosts_ptr = hosts_ptr->next;
	}

	update++;
}

/*** sigusr_callback ***/
static void sigusr_callback(int signal) {
	write_log(1, LOG_NOTICE, "Received signal '%s', dumping state.", strsignal(signal));

	dump_state();
}

/*** main ***/
int main(int argc, char ** argv) {
	dictionary * ini;
	const char * inistring;
	char * values, * value;
	uint16_t port;
	struct interfaces * interfaces_ptr;
	int i, ret = 1, sleepsec = 0;
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

	/* are we running from systemd? */
	if (sd_notify(0, "READY=0") > 0)
		systemd = 1;

	write_log(verbose, LOG_NOTICE, "%s: " PROGNAME " v" VERSION " " ID "/" ARCH
			" (built: " __DATE__ ", " __TIME__ ")", argv[0]);

	if (help > 0)
		write_log(1, LOG_NOTICE, "usage: %s [-h] [-v] [-V]", argv[0]);

	if (version > 0 || help > 0)
		return EXIT_SUCCESS;

	if (getuid() == 0) {
		/* process is running as root, drop privileges */
		write_log(verbose, LOG_WARNING, "Running as root, meh! Dropping privileges.");
		if (setgid(DROP_PRIV_GID) != 0 || setuid(DROP_PRIV_UID) != 0)
			write_log(1, LOG_ERR, "Unable to drop user privileges!");
	}

	/* allocate first struct element as dummy */
	hosts = malloc(sizeof(struct hosts));
	hosts->host = NULL;
	hosts->online = 0;
	hosts->badtime = 0;
	hosts->next = NULL;

	interfaces = malloc(sizeof(struct interfaces));
	interfaces->interface = NULL;
	interfaces->ifindex = 0;
	interfaces->next = NULL;

	/* Probing for static pacserve hosts takes some time.
	 * Receiving a SIGHUP at this time could kill us. So register signal
	 * SIGHUP here before probing. */
	struct sigaction act_hup = { 0 };
	act_hup.sa_handler = sighup_callback;
	sigaction(SIGHUP, &act_hup, NULL);

	/* parse config file */
	if ((ini = iniparser_load(PACREDIR_CONF)) == NULL) {
		write_log(1, LOG_WARNING, "cannot parse file " PACREDIR_CONF ", continue anyway");
		/* continue anyway, there is nothing essential in the config file */
	} else {
		int ini_verbose;

		/* extra verbosity from config */
		ini_verbose = iniparser_getint(ini, ":verbose", 0);
		verbose += ini_verbose;

		/* store interfaces to ignore */
		if ((inistring = iniparser_getstring(ini, ":ignore interfaces", NULL)) != NULL) {
			values = strdup(inistring);
			interfaces_ptr = interfaces;

			value = strtok(values, DELIMITER);
			while (value != NULL) {
				write_log(verbose, LOG_INFO, "Ignoring interface: %s", value);
				add_interface(value, 0, 0, 1);
				value = strtok(NULL, DELIMITER);
			}
			free(values);
		}

		/* add static pacserve hosts */
		if ((inistring = iniparser_getstring(ini, ":pacserve hosts", NULL)) != NULL) {
			values = strdup(inistring);
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				write_log(verbose, LOG_NOTICE, "Adding static host: %s", value);

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
		write_log(1, LOG_ERR, "Could not start daemon on port %d.", PORT_PACREDIR);
		goto fail;
	}

	write_log(verbose, LOG_NOTICE, "Listening on port %d", PORT_PACREDIR);

	/* initialize curl */
	curl_global_init(CURL_GLOBAL_ALL);

	/* register SIG{INT,TERM} signal callbacks */
	struct sigaction act = { 0 };
	act.sa_handler = sig_callback;
	sigaction(SIGINT,  &act, NULL);
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
		sleepsec = sleep(sleepsec);

		if (sleepsec > 0 && update == 0)
			continue;

		update_interfaces();
		update_hosts();
		update = 0;
		sleepsec = 60;
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
	free(hosts);

	while (interfaces->interface != NULL) {
		free(interfaces->interface);
		interfaces_ptr = interfaces->next;
		free(interfaces);
		interfaces = interfaces_ptr;
	}
	free(interfaces);

	sd_notify(0, "STATUS=Stopped. Bye!");

	return ret;
}
