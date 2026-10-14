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

/*** find_best_redirect ***/
static struct request * find_best_redirect(const char * basename, uint8_t dbfile, time_t last_modified) {
	struct hosts * hosts_ptr = hosts;

	int req_idx = -1;
	struct request * best = NULL,
		** requests = NULL,
		* request = NULL;
	double best_time = INFINITY;
	char ctime[26];
	struct timeval tv;

	CURLM *curlm;
	CURLMsg *msg;
	int msgs_left, still_running = 1;

	gettimeofday(&tv, NULL);

	if ((curlm = curl_multi_init()) == NULL) {
		write_log(1, LOG_ERR, "Failed initializing curl multi handle.");
		return NULL;
	}

	/* prepare the requests */
	while (hosts_ptr->host != NULL) {
		CURL *curl = NULL;
		time_t badtime = hosts_ptr->badtime + hosts_ptr->badcount * BADTIME;

		/* skip host if offline */
		if (hosts_ptr->online == 0) {
			write_log(verbose, LOG_INFO, "Host %s is offline, skipping", hosts_ptr->host);
			hosts_ptr = hosts_ptr->next;
			continue;
		}

		/* skip host if had a bad request within last BADTIME seconds */
		if (badtime > tv.tv_sec) {
			/* write the time to buffer ctime, then strip the line break */
			ctime_r(&badtime, ctime);
			ctime[strlen(ctime) - 1] = '\0';

			write_log(verbose, LOG_INFO, "Host %s is marked bad until %s, skipping.",
					hosts_ptr->host, ctime);

			hosts_ptr = hosts_ptr->next;
			continue;
		}

		req_idx++;
		requests = realloc(requests, sizeof(size_t) * (req_idx + 1));
		request = requests[req_idx] = malloc(sizeof(struct request));

		/* prepare request struct */
		request->host = hosts_ptr;
		request->url = get_url(request->host->host, request->host->port, dbfile, basename);
		request->http_code = 0;
		request->last_modified = 0;
		request->curl = curl_easy_init();
		*request->errbuf = 0;

		if ((curl = request->curl)  == NULL) {
			write_log(1, LOG_ERR, "Failed initializing curl_easy!");
			goto curl_easy_fail;
		}

		write_log(verbose, LOG_INFO, "Trying %s: %s", request->host->host, request->url);

		curl_easy_setopt(curl, CURLOPT_URL, request->url);
		/* try to resolve addresses to all IP versions that your system allows */
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE,
			request->host->mdns ? CURL_IPRESOLVE_V4 : CURL_IPRESOLVE_WHATEVER);
		/* tell libcurl to follow redirection */
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		/* set user agent */
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "pacredir/" VERSION " (" ID "/" ARCH ")");
		/* do not receive body */
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
		/* ask for filetime */
		curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
		/* set connection timeout to 2 seconds
		 * if the host needs longer we do not want to use it anyway ;) */
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
		/* time out if connection is established but transfer rate is low
		 * this should make curl finish after a maximum of 8 seconds */
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 3L);
		/* skip all signal handling */
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		/* provide a buffer to store errors in */
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, request->errbuf);

		/* add easy handle to multi handle */
		curl_multi_add_handle(curlm, curl);

		hosts_ptr = hosts_ptr->next;
	}

	/* perform all the requests */
	while (still_running) {
		CURLMcode mresult = curl_multi_perform(curlm, &still_running);

		if (still_running)
			/* wait for activity, timeout or "nothing" */
			mresult = curl_multi_poll(curlm, NULL, 0, 1000, NULL);

		if (mresult)
			break;
	}

	/* see how the transfers went */
	while ((msg = curl_multi_info_read(curlm, &msgs_left)) != NULL) {
		CURL *curl = NULL;
		CURLcode res;

		if (msg->msg != CURLMSG_DONE)
			continue;

		/* Find out which handle this message is about */
		for (int idx = 0; idx <= req_idx; idx++) {
			request = requests[idx];
			curl = request->curl;
			if (msg->easy_handle == curl)
				break;
		}

		/* something went wrong... */
		if (msg->data.result != CURLE_OK) {
			write_log(1, LOG_WARNING, "Could not connect to peer %s on port %d: %s",
				request->host->host, request->host->port,
				*request->errbuf != 0 ? request->errbuf : curl_easy_strerror(msg->data.result));
			request->host->badtime = tv.tv_sec;
			request->host->badcount++;
			goto request_free;
		}

		request->host->badtime = 0;
		request->host->badcount = 0;

		/* get http status code */
		if ((res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(request->http_code))) != CURLE_OK) {
			write_log(1, LOG_ERR, "curl_easy_getinfo() failed: %s", curl_easy_strerror(res));
			goto request_free;
		}

		/* get total time */
		if ((res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &(request->time_total))) != CURLE_OK) {
			write_log(1, LOG_ERR, "curl_easy_getinfo() failed: %s", curl_easy_strerror(res));
			goto request_free;
		}

		/* get last modified time and content length */
		if (request->http_code == MHD_HTTP_OK) {
			if ((res = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(request->last_modified))) != CURLE_OK) {
				write_log(1, LOG_ERR, "curl_easy_getinfo() failed: %s", curl_easy_strerror(res));
				goto request_free;
			}

			if ((res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &(request->content_length))) != CURLE_OK) {
				write_log(1, LOG_ERR, "curl_easy_getinfo() failed: %s", curl_easy_strerror(res));
				goto request_free;
			}
		}

		/* skip if http code not OK, but clean up */
		if (request->http_code != MHD_HTTP_OK) {
			write_log(verbose, LOG_INFO, "Received HTTP status code %d from %s",
					request->http_code, request->host->host);
			goto request_free;
		}

		/* found the file! */
		request->host->finds++;
		/* write the time to buffer ctime, then strip the line break */
		ctime_r(&request->last_modified, ctime);
		ctime[strlen(ctime) - 1] = '\0';

		write_log(verbose, LOG_INFO, "Found on %s (%f sec, modified: %s)",
				request->host->host, request->time_total, ctime);

		if	/* for db files choose the most recent peer when not too old */
			((dbfile == 1 && ((request->last_modified > last_modified &&
					   request->last_modified + 86400 > time(NULL)) ||
			/* but use a faster peer if available */
					  (best->url != NULL &&
					   request->last_modified >= last_modified &&
					   request->time_total < best_time))) ||
			/* for packages try to guess the fastest peer */
			 (dbfile == 0 && request->time_total < best_time)) {
			best_time = request->time_total;
			if (best != NULL) {
				free(best->url);
				free(best);
			}
			best = request;
		}

request_free:
		curl_multi_remove_handle(curlm, curl);
		curl_easy_cleanup(curl);

		if (request != best) {
			free(request->url);
			free(request);
		}
	}

	if (best == NULL) {
		if (req_idx < 0)
			write_log(verbose, LOG_NOTICE, "Currently no peers are available to check for %s.",
					basename);
		else if (dbfile > 0)
			write_log(verbose, LOG_NOTICE, "No more recent version of %s found on %d peers.",
					basename, req_idx + 1);
		else
			write_log(verbose, LOG_NOTICE, "File %s not found on %d peers, giving up.",
					basename, req_idx + 1);
	}

curl_easy_fail:
	free(requests);
	curl_multi_cleanup(curlm);

	return best;
}

