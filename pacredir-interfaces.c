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

/*** add_interface ***/
static struct interfaces * add_interface(const char * interface) {
	struct interfaces * interfaces_ptr = interfaces, * previous = NULL, * new;
	struct patterns * ignore_patterns_ptr = ignore_patterns;

	while (interfaces_ptr->interface != NULL) {
		int cmp = strcmp(interfaces_ptr->interface, interface);

		/* skip to next */
		if (cmp < 0) {
			previous = interfaces_ptr;
			interfaces_ptr = interfaces_ptr->next;
			continue;
		}

		/* interface already exists */
		if (cmp == 0)
			return interfaces_ptr;

		/* insert before, or first */
		if (cmp > 0) {
			if ((new = malloc(sizeof(struct interfaces))) == NULL)
				return NULL;

			new->next = interfaces_ptr;

			/* first! */
			if (interfaces_ptr == interfaces) {
				interfaces_ptr = interfaces = new;
				goto new;
			}

			/* before, but not first */
			interfaces_ptr = previous->next = new;
			goto new;
		}
	}

	/* append at the end */
	if ((new = malloc(sizeof(struct interfaces))) == NULL)
		return NULL;

	new->interface = NULL;
	new->next = NULL;
	interfaces_ptr->next = new;

new:
	/* interface not found, adding a new one */
	write_log(verbose, LOG_INFO, "Adding interface %s", interface);

	interfaces_ptr->interface = strdup(interface);
	interfaces_ptr->ifindex = 0;
	interfaces_ptr->up = 0;
	interfaces_ptr->running = 0;
	interfaces_ptr->ignore = 0;

	while (ignore_patterns_ptr->pattern != NULL) {
		if (fnmatch(ignore_patterns_ptr->pattern, interface, 0) == 0) {
			write_log(verbose, LOG_INFO, "Ignoring interface %s by pattern", interface);
			interfaces_ptr->ignore = 1;
			break;
		}
		ignore_patterns_ptr = ignore_patterns_ptr->next;
	}

	return interfaces_ptr;
}

/*** ignore_interface ***/
static struct interfaces * ignore_interface(const char * interface) {
	struct interfaces * interfaces_ptr;

	if ((interfaces_ptr = add_interface(interface)) == NULL)
		return NULL;

	interfaces_ptr->ignore = 1;

	return interfaces_ptr;
}

/*** update_interface ***/
static struct interfaces * update_interface(const char * interface, const uint8_t up, const uint8_t running) {
	struct interfaces * interfaces_ptr;

	if ((interfaces_ptr = add_interface(interface)) == NULL)
		return NULL;

	interfaces_ptr->ifindex = if_nametoindex(interface);
	interfaces_ptr->up = up;
	interfaces_ptr->running = running;

	return interfaces_ptr;
}

/*** add_ignore_pattern ***/
static struct patterns * add_ignore_pattern(const char * pattern) {
	struct patterns * patterns_ptr = ignore_patterns, * new;

	while (patterns_ptr->pattern != NULL)
		patterns_ptr = patterns_ptr->next;

	if ((new = malloc(sizeof(struct patterns))) == NULL)
		return NULL;

	new->pattern = NULL;
	new->next = NULL;
	patterns_ptr->next = new;
	patterns_ptr->pattern = strdup(pattern);

	return patterns_ptr;
}

/*** update_interfaces ***/
static void update_interfaces(void) {
	struct if_nameindex *if_nidxs, *intf;
	int sock;

	if ((if_nidxs = if_nameindex()) == NULL) {
		write_log(1, LOG_ERR, "Failed to get list of interfaces.");
		return;
	}

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		write_log(1, LOG_ERR, "Failed to open control socket.");
		return;
	}

	for (intf = if_nidxs; intf->if_index != 0 || intf->if_name != NULL; intf++) {
		struct ifreq ifr;

		memset(&ifr, 0, sizeof(struct ifreq));
		strncpy(ifr.ifr_name, intf->if_name, IFNAMSIZ);
		ifr.ifr_name[IFNAMSIZ-1] = 0;

		if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
			write_log(1, LOG_ERR, "Failed to get flags for interface %s.", intf->if_name);
			continue;
		}

		if ((ifr.ifr_flags & IFF_LOOPBACK) > 0)
			continue;

		if (update_interface(intf->if_name, ifr.ifr_flags & IFF_UP, ifr.ifr_flags & IFF_RUNNING) == NULL)
			write_log(1, LOG_ERR, "Updating interface %s failed!", intf->if_name);
	}

	close(sock);
	if_freenameindex(if_nidxs);
}

/*** free_interfaces ***/
static void free_interfaces(void) {
	struct interfaces * previous;

	while (interfaces->interface != NULL) {
		previous = interfaces;
		interfaces = interfaces->next;
		free(previous->interface);
		free(previous);
	}
	free(interfaces);
	interfaces = NULL;
}

/*** free_ignore_patterns ***/
static void free_ignore_patterns(void) {
	struct patterns * previous;

	while (ignore_patterns->pattern != NULL) {
		previous = ignore_patterns;
		ignore_patterns = ignore_patterns->next;
		free(previous->pattern);
		free(previous);
	}
	free(ignore_patterns);
	ignore_patterns = NULL;
}
