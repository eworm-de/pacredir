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
