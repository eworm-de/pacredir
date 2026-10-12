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
	struct interfaces *interfaces_ptr = interfaces;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	sd_bus *bus = NULL;
	int r;

	/* set 'present' to 0, so we later know which hosts were available, and which were not */
	while (hosts_ptr->host != NULL) {
		hosts_ptr->present = 0;
		hosts_ptr = hosts_ptr->next;
	}

	r = sd_bus_open_system(&bus);
	if (r < 0) {
		write_log(1, LOG_ERR, "Failed to open system bus: %s", strerror(-r));
		goto fast_finish;
	}

	r = sd_bus_call_method(bus, "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
		"org.freedesktop.resolve1.Manager", "ResolveRecord", &error,
		&reply, "isqqt", 0 /* any */, PACSERVE "." MDNS_DOMAIN,
		DNS_CLASS_IN, DNS_TYPE_PTR, SD_RESOLVED_NO_SYNTHESIZE|SD_RESOLVED_NO_ZONE);
	if (r < 0) {
		/* EAGAIN is returned when no mDNS hosts are available - so harmless */
		write_log((errno ^ EAGAIN) | verbose, errno ^ EAGAIN ? LOG_ERR : LOG_DEBUG,
			"Failed to trigger caching record: %s (%s)",
			error.message, strerror(errno));
		sd_bus_error_free(&error);
		goto finish;
	}

	/* On empty cache systemd-resolved returns just one record on first query,
	   happened above. Similar delays are seen for new hosts.
	   Wait a moment, call again for full update, then goto finish.
	   TODO: Drop when continuous mDNS querying becomes available! */
	usleep(250000);

	while (interfaces_ptr->interface != NULL) {
		if (interfaces_ptr->up && interfaces_ptr->running && !interfaces_ptr->ignore)
			update_hosts_on_interface(bus, interfaces_ptr->ifindex, interfaces_ptr->interface);

		if (quit)
			break;

		interfaces_ptr = interfaces_ptr->next;
	}

finish:
	/* mark hosts offline that did not show up in query */
	hosts_ptr = hosts;
	while (hosts_ptr->host != NULL) {
		if (hosts_ptr->mdns == 1 && hosts_ptr->online == 1 && hosts_ptr->present == 0) {
			write_log(verbose, LOG_NOTICE, "Marking host %s offline", hosts_ptr->host);
			hosts_ptr->online = 0;
		}
		hosts_ptr = hosts_ptr->next;
	}

fast_finish:
	sd_bus_message_unref(reply);
	sd_bus_flush_close_unref(bus);
}

/*** update_hosts_on_interface ***/
static void update_hosts_on_interface(sd_bus *bus, const unsigned int if_index, const char *if_name) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply_record = NULL;
	uint64_t flags;
	int r;

	r = sd_bus_call_method(bus, "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
		"org.freedesktop.resolve1.Manager", "ResolveRecord", &error,
		&reply_record, "isqqt", if_index, PACSERVE "." MDNS_DOMAIN,
		DNS_CLASS_IN, DNS_TYPE_PTR, SD_RESOLVED_NO_SYNTHESIZE|SD_RESOLVED_NO_ZONE);
	if (r < 0) {
		/* EAGAIN is returned when no mDNS hosts are available - so harmless */
		write_log((errno ^ EAGAIN) | verbose, errno ^ EAGAIN ? LOG_ERR : LOG_DEBUG,
			"Failed to resolve record on %s: %s (%s)",
			if_name, error.message, strerror(errno));
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
		char *peer;
		const char *canonical, *discard;
		uint8_t match = 0;

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
		peer = process_reply_record(data, length);

		sd_bus_message *reply_service = NULL;
		/* service START */
		r = sd_bus_call_method(bus, "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
			"org.freedesktop.resolve1.Manager", "ResolveService", &error,
			&reply_service, "isssit", if_index, "", "", peer, AF_UNSPEC, UINT64_C(0));
		if (r < 0) {
			write_log(1, LOG_ERR, "Failed to resolve service '%s' on %s: %s (%s)",
				peer, if_name, error.message, strerror(errno));
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

		if (match < DNS_SRV_TXT_MATCH_ALL) {
			write_log(verbose, LOG_INFO, "Host %s does not match distribution and/or architecture.", canonical);
			goto finish_service;
		}

		/* add the peer to our struct */
		add_host(canonical, port, 1);

		goto finish_service;

parse_failure_service:
		write_log(1, LOG_ERR, "Parse failure for service: %s", strerror(-r));

finish_service:
		free(peer);
		sd_bus_message_unref(reply_service);
	}

	r = sd_bus_message_exit_container(reply_record);
	if (r < 0)
		goto parse_failure_record;
	r = sd_bus_message_read(reply_record, "t", &flags);
	if (r < 0)
		goto parse_failure_record;

	goto finish;

parse_failure_record:
	write_log(1, LOG_ERR, "Parse failure for record: %s", strerror(-r));

finish:
	sd_bus_message_unref(reply_record);
}

/*** add_host ***/
static int add_host(const char * host, const uint16_t port, const uint8_t mdns) {
	struct hosts * hosts_ptr = hosts, * previous = NULL;

	while (hosts_ptr->host != NULL) {
		int cmp = strcmp(hosts_ptr->host, host);

		/* skip to next */
		if (cmp < 0) {
			previous = hosts_ptr;
			hosts_ptr = hosts_ptr->next;
			continue;
		}

		/* host already exists */
		if (cmp == 0) {
			/* ... but with different port */
			if (hosts_ptr->port != port) {
				previous = hosts_ptr;
				hosts_ptr = hosts_ptr->next;
				continue;
			}

			if (hosts_ptr->online < 1)
				write_log(verbose, LOG_NOTICE, "Marking host %s online", host);
			goto update;
		}

		/* insert before, or first */
		if (cmp > 0) {
			struct hosts * new = malloc(sizeof(struct hosts));
			new->next = hosts_ptr;

			/* first! */
			if (hosts_ptr == hosts) {
				hosts_ptr = hosts = new;
				goto new;
			}

			/* before, but not first */
			hosts_ptr = previous->next = new;
			goto new;
		}
	}

	/* append at the end */
	hosts_ptr->next = malloc(sizeof(struct hosts));
	hosts_ptr->next->host = NULL;
	hosts_ptr->next->next = NULL;

new:
	/* host not found, adding a new one */
	write_log(verbose, mdns ? LOG_NOTICE : LOG_INFO, "Adding host %s with port %d", host, port);

	hosts_ptr->host = strdup(host);
	hosts_ptr->port = port;
	hosts_ptr->mdns = mdns;
	hosts_ptr->badtime = 0;
	hosts_ptr->badcount = 0;
	hosts_ptr->finds = 0;
	hosts_ptr->redir = 0;

update:
	hosts_ptr->online = 1;
	hosts_ptr->present = 1;

	return EXIT_SUCCESS;
}
