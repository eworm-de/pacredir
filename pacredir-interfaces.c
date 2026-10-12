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
static int add_interface(const char * interface, const uint8_t up, const uint8_t running, const uint8_t ignore) {
	struct interfaces * interfaces_ptr = interfaces, * previous = NULL;

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
			goto update;

		/* insert before, or first */
		if (cmp > 0) {
			struct interfaces * new = malloc(sizeof(struct interfaces));
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
	interfaces_ptr->next = malloc(sizeof(struct interfaces));
	interfaces_ptr->next->interface = NULL;
	interfaces_ptr->next->next = NULL;

new:
	/* interface not found, adding a new one */
	write_log(verbose, LOG_INFO, "Adding interface %s", interface);

	interfaces_ptr->interface = strdup(interface);
	interfaces_ptr->ignore = ignore;

update:
	interfaces_ptr->ifindex = if_nametoindex(interface);
	interfaces_ptr->up = up;
	interfaces_ptr->running = running;

	return EXIT_SUCCESS;
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

		add_interface(intf->if_name, ifr.ifr_flags & IFF_UP, ifr.ifr_flags & IFF_RUNNING, 0);
	}

	close(sock);
	if_freenameindex(if_nidxs);
}
