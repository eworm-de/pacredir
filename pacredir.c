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
struct patterns * ignore_patterns = NULL;
uint8_t quit = 0, systemd = 0, update = 0, verbose = 0;
unsigned int count_redirect = 0, count_not_found = 0;

/* include more code */
#include "pacredir-hosts.c"
#include "pacredir-interfaces.c"
#include "pacredir-request.c"
#include "pacredir-server.c"

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
	write_log(1, LOG_NOTICE, "Received signal '%s', resetting bad counts, updating interfaces and hosts.",
		strsignal(signal));

	reset_hosts();
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
	int i, ret = 1, sleepsec = 0;
	struct MHD_Daemon * mhd;
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
	hosts->next = NULL;

	interfaces = malloc(sizeof(struct interfaces));
	interfaces->interface = NULL;
	interfaces->next = NULL;

	ignore_patterns = malloc(sizeof(struct patterns));
	ignore_patterns->pattern = NULL;
	ignore_patterns->next = NULL;

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
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				write_log(verbose, LOG_INFO, "Ignoring interface: %s", value);
				if (ignore_interface(value) == NULL)
					write_log(1, LOG_ERR, "Adding interface %s failed!", value);
				value = strtok(NULL, DELIMITER);
			}
			free(values);
		}

		/* store patterns for interfaces to ignore */
		if ((inistring = iniparser_getstring(ini, ":ignore patterns", NULL)) != NULL) {
			values = strdup(inistring);
			value = strtok(values, DELIMITER);
			while (value != NULL) {
				write_log(verbose, LOG_INFO, "Adding ignore pattern: %s", value);
				if (add_ignore_pattern(value) == NULL)
					write_log(1, LOG_ERR, "Adding ignore pattern %s failed!", value);
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
				if (add_host(value, port, 0) == NULL)
					write_log(1, LOG_ERR, "Adding host %s failed!", value);
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
	free_hosts();
	free_interfaces();
	free_ignore_patterns();

	sd_notify(0, "STATUS=Stopped. Bye!");

	return ret;
}
