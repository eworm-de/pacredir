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

#include "pacserve-announce.h"

/* global variables */
uint8_t quit = 0;

/*** sig_callback ***/
static void sig_callback(int signal) {
	switch (signal) {
		case SIGINT:
		case SIGKILL:
		case SIGTERM:
			break;
		default:
			/* just ignore */
			return;
	}

	fprintf(stderr, "Received signal '%s', quitting.", strsignal(signal));
	quit++;
}                

/*** prepare message ***/
static int prepare_message(sd_bus_message *message, uint16_t port) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r;

	/* append to bus message - general info */
	r = sd_bus_message_append(message, "sssqqq", "pacserve", "pacserve on %H", PACSERVE, port, 0 /* priority */, 0 /* weight */);
	if (r < 0) {
		fprintf(stderr, "Failed to append to bus message: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}

	/* array - outher */
	r = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "a{say}");
	if (r < 0) {
		fprintf(stderr, "Failed to open container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	/* array - inner */
	r = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{say}");
	if (r < 0) {
		fprintf(stderr, "Failed to open container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}

	/* dictionary - 1st */
	r = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "say");
	if (r < 0) {
		fprintf(stderr, "Failed to open container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_append(message, "s", "id");
	if (r < 0) {
		fprintf(stderr, "Failed to append to bus message: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_append_array(message, 'y', ID, sizeof(ID));
	if (r < 0) {
		fprintf(stderr, "Failed to append array to bus message: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_close_container(message);
	if (r < 0) {
		fprintf(stderr, "Failed to close container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}

	/* dictionary - 2nd */
	r = sd_bus_message_open_container(message, SD_BUS_TYPE_DICT_ENTRY, "say");
	if (r < 0) {
		fprintf(stderr, "Failed to open container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_append(message, "s", "arch");
	if (r < 0) {
		fprintf(stderr, "Failed to append to bus message: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_append_array(message, 'y', ARCH, sizeof(ARCH));
	if (r < 0) {
		fprintf(stderr, "Failed to append array to bus message: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_close_container(message);
	if (r < 0) {
		fprintf(stderr, "Failed to close container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}

	/* close inner and outher array */
	r = sd_bus_message_close_container(message);
	if (r < 0) {
		fprintf(stderr, "Failed to close container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}
	r = sd_bus_message_close_container(message);
	if (r < 0) {
		fprintf(stderr, "Failed to close container: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		return r;
	}

	return r;
}

/*** main ***/
int main(int argc, char ** argv) {
	dictionary * ini;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *message = NULL, *reply = NULL;
	sd_bus *bus = NULL;
	int ret = EXIT_FAILURE, r;
	uint16_t port = PORT_PACSERVE;
	char *path = NULL;

	/* register signal callbacks */
	struct sigaction act = { 0 };
	act.sa_handler = sig_callback;
	sigaction(SIGHUP,  &act, NULL);
	sigaction(SIGINT,  &act, NULL);
	sigaction(SIGKILL, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGUSR1, &act, NULL);
	sigaction(SIGUSR2, &act, NULL);

	if ((ini = iniparser_load(PACSERVE_CONF)) == NULL) {
		fprintf(stderr, "cannot parse file " PACSERVE_CONF ", continue anyway");
		/* continue anyway, there is nothing essential in the config file */
	} else {
		port = iniparser_getint(ini, ":port", PORT_PACSERVE);

		/* done reading config file, free */
		iniparser_freedict(ini);
	}

	/* open message bus */
	r = sd_bus_open_system(&bus);
	if (r < 0) {
		fprintf(stderr, "Failed to open system bus: %s", strerror(-r));
		goto fail;
	}

	/* new method call */
	r = sd_bus_message_new_method_call(bus, &message,
		"org.freedesktop.resolve1", "/org/freedesktop/resolve1",
		"org.freedesktop.resolve1.Manager", "RegisterService");
	if (r < 0) {
		fprintf(stderr, "Failed new method call: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		goto fail;
	}

	/* prepare the message */
	r = prepare_message(message, port);
	if (r < 0) {
		fprintf(stderr, "Failed to preare bus message.\n");
		goto fail;
	}

	/* send the message to bus */
	r = sd_bus_call(bus, message, -1, &error, &reply);
	if (r < 0) {
		fprintf(stderr, "Failed to call: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		goto fail;
	}
	sd_bus_message_unref(message);

	/* report ready to systemd */
	sd_notify(0, "READY=1\nSTATUS=Announced!");

	/* read the reply for unregister */
	r = sd_bus_message_read(reply, "o", &path);
	if (r < 0) 
		goto fail;
	sd_bus_message_unref(reply);

	/* main loop */
	while (quit == 0)
		sleep(UINT_MAX);

	/* report stopping to systemd */
	sd_notify(0, "STOPPING=1\nSTATUS=Stopping...");

	/* unregister the service */
	r = sd_bus_call_method(bus,
		"org.freedesktop.resolve1", "/org/freedesktop/resolve1",
		"org.freedesktop.resolve1.Manager", "UnregisterService", &error, &reply,
		"o", path);
	if (r < 0) {
		fprintf(stderr, "Failed to unregister service: %s (%s)\n", error.message, strerror(errno));
		sd_bus_error_free(&error);
		goto fail;
	}
	sd_bus_message_unref(reply);

	ret = EXIT_SUCCESS;

fail:
	sd_bus_flush_close_unref(bus);

	sd_notify(0, "STATUS=Stopped. Bye!");

	return ret;
}
