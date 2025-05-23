/*****************************************************************************
 *
 * Monitoring check_real plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_real plugin
 *
 * This plugin tests the REAL service on the specified host.
 *
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *****************************************************************************/

#include "states.h"
#include <stdio.h>
const char *progname = "check_real";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "check_real.d/config.h"

#define EXPECT "RTSP/1."
#define URL    ""

typedef struct {
	int errorcode;
	check_real_config config;
} check_real_config_wrapper;
static check_real_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static void print_help(void);
void print_usage(void);

static bool verbose = false;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_real_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_real_config config = tmp_config.config;

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);
	time(&start_time);

	/* try to connect to the host at the given port number */
	int socket;
	if (my_tcp_connect(config.server_address, config.server_port, &socket) != STATE_OK) {
		die(STATE_CRITICAL, _("Unable to connect to %s on port %d\n"), config.server_address, config.server_port);
	}

	/* Part I - Server Check */

	/* send the OPTIONS request */
	char buffer[MAX_INPUT_BUFFER];
	sprintf(buffer, "OPTIONS rtsp://%s:%d RTSP/1.0\r\n", config.host_name, config.server_port);
	ssize_t sent_bytes = send(socket, buffer, strlen(buffer), 0);
	if (sent_bytes == -1) {
		die(STATE_CRITICAL, _("Sending options to %s failed\n"), config.host_name);
	}

	/* send the header sync */
	sprintf(buffer, "CSeq: 1\r\n");
	sent_bytes = send(socket, buffer, strlen(buffer), 0);
	if (sent_bytes == -1) {
		die(STATE_CRITICAL, _("Sending header sync to %s failed\n"), config.host_name);
	}

	/* send a newline so the server knows we're done with the request */
	sprintf(buffer, "\r\n");
	sent_bytes = send(socket, buffer, strlen(buffer), 0);
	if (sent_bytes == -1) {
		die(STATE_CRITICAL, _("Sending newline to %s failed\n"), config.host_name);
	}

	/* watch for the REAL connection string */
	ssize_t received_bytes = recv(socket, buffer, MAX_INPUT_BUFFER - 1, 0);

	/* return a CRITICAL status if we couldn't read any data */
	if (received_bytes == -1) {
		die(STATE_CRITICAL, _("No data received from %s\n"), config.host_name);
	}

	mp_state_enum result = STATE_OK;
	char *status_line = NULL;
	/* make sure we find the response we are looking for */
	if (!strstr(buffer, config.server_expect)) {
		if (config.server_port == PORT) {
			printf("%s\n", _("Invalid REAL response received from host"));
		} else {
			printf(_("Invalid REAL response received from host on port %d\n"), config.server_port);
		}
	} else {
		/* else we got the REAL string, so check the return code */

		time(&end_time);

		result = STATE_OK;

		status_line = strtok(buffer, "\n");

		if (strstr(status_line, "200")) {
			result = STATE_OK;
		}

		/* client errors result in a warning state */
		else if (strstr(status_line, "400")) {
			result = STATE_WARNING;
		} else if (strstr(status_line, "401")) {
			result = STATE_WARNING;
		} else if (strstr(status_line, "402")) {
			result = STATE_WARNING;
		} else if (strstr(status_line, "403")) {
			result = STATE_WARNING;
		} else if (strstr(status_line, "404")) {
			result = STATE_WARNING;
		} else if (strstr(status_line, "500")) {
			/* server errors result in a critical state */
			result = STATE_CRITICAL;
		} else if (strstr(status_line, "501")) {
			result = STATE_CRITICAL;
		} else if (strstr(status_line, "502")) {
			result = STATE_CRITICAL;
		} else if (strstr(status_line, "503")) {
			result = STATE_CRITICAL;
		} else {
			result = STATE_UNKNOWN;
		}
	}

	/* Part II - Check stream exists and is ok */
	if ((result == STATE_OK) && (config.server_url != NULL)) {

		/* Part I - Server Check */

		/* send the DESCRIBE request */
		sprintf(buffer, "DESCRIBE rtsp://%s:%d%s RTSP/1.0\r\n", config.host_name, config.server_port, config.server_url);

		ssize_t sent_bytes = send(socket, buffer, strlen(buffer), 0);
		if (sent_bytes == -1) {
			die(STATE_CRITICAL, _("Sending DESCRIBE request to %s failed\n"), config.host_name);
		}

		/* send the header sync */
		sprintf(buffer, "CSeq: 2\r\n");
		sent_bytes = send(socket, buffer, strlen(buffer), 0);
		if (sent_bytes == -1) {
			die(STATE_CRITICAL, _("Sending DESCRIBE request to %s failed\n"), config.host_name);
		}

		/* send a newline so the server knows we're done with the request */
		sprintf(buffer, "\r\n");
		sent_bytes = send(socket, buffer, strlen(buffer), 0);
		if (sent_bytes == -1) {
			die(STATE_CRITICAL, _("Sending DESCRIBE request to %s failed\n"), config.host_name);
		}

		/* watch for the REAL connection string */
		ssize_t recv_bytes = recv(socket, buffer, MAX_INPUT_BUFFER - 1, 0);
		if (recv_bytes == -1) {
			/* return a CRITICAL status if we couldn't read any data */
			printf(_("No data received from host\n"));
			result = STATE_CRITICAL;
		} else {
			buffer[result] = '\0'; /* null terminate received buffer */
			/* make sure we find the response we are looking for */
			if (!strstr(buffer, config.server_expect)) {
				if (config.server_port == PORT) {
					printf("%s\n", _("Invalid REAL response received from host"));
				} else {
					printf(_("Invalid REAL response received from host on port %d\n"), config.server_port);
				}
			} else {

				/* else we got the REAL string, so check the return code */

				time(&end_time);

				result = STATE_OK;

				status_line = strtok(buffer, "\n");

				if (strstr(status_line, "200")) {
					result = STATE_OK;
				}

				/* client errors result in a warning state */
				else if (strstr(status_line, "400")) {
					result = STATE_WARNING;
				} else if (strstr(status_line, "401")) {
					result = STATE_WARNING;
				} else if (strstr(status_line, "402")) {
					result = STATE_WARNING;
				} else if (strstr(status_line, "403")) {
					result = STATE_WARNING;
				} else if (strstr(status_line, "404")) {
					result = STATE_WARNING;
				}

				/* server errors result in a critical state */
				else if (strstr(status_line, "500")) {
					result = STATE_CRITICAL;
				} else if (strstr(status_line, "501")) {
					result = STATE_CRITICAL;
				} else if (strstr(status_line, "502")) {
					result = STATE_CRITICAL;
				} else if (strstr(status_line, "503")) {
					result = STATE_CRITICAL;
				}

				else {
					result = STATE_UNKNOWN;
				}
			}
		}
	}

	/* Return results */
	if (result == STATE_OK) {
		if (config.check_critical_time && (end_time - start_time) > config.critical_time) {
			result = STATE_CRITICAL;
		} else if (config.check_warning_time && (end_time - start_time) > config.warning_time) {
			result = STATE_WARNING;
		}

		/* Put some HTML in here to create a dynamic link */
		printf(_("REAL %s - %d second response time\n"), state_text(result), (int)(end_time - start_time));
	} else {
		printf("%s\n", status_line);
	}

	/* close the connection */
	close(socket);

	/* reset the alarm */
	alarm(0);

	exit(result);
}

/* process command-line arguments */
check_real_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'}, {"IPaddress", required_argument, 0, 'I'},
									   {"expect", required_argument, 0, 'e'},   {"url", required_argument, 0, 'u'},
									   {"port", required_argument, 0, 'p'},     {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},  {"timeout", required_argument, 0, 't'},
									   {"verbose", no_argument, 0, 'v'},        {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},           {0, 0, 0, 0}};

	check_real_config_wrapper result = {
		.errorcode = OK,
		.config = check_real_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0) {
			strcpy(argv[i], "-t");
		} else if (strcmp("-wt", argv[i]) == 0) {
			strcpy(argv[i], "-w");
		} else if (strcmp("-ct", argv[i]) == 0) {
			strcpy(argv[i], "-c");
		}
	}

	while (true) {
		int option = 0;
		int option_char = getopt_long(argc, argv, "+hvVI:H:e:u:p:w:c:t:", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
			break;
		}

		switch (option_char) {
		case 'I': /* hostname */
		case 'H': /* hostname */
			if (result.config.server_address) {
				break;
			} else if (is_host(optarg)) {
				result.config.server_address = optarg;
			} else {
				usage2(_("Invalid hostname/address"), optarg);
			}
			break;
		case 'e': /* string to expect in response header */
			result.config.server_expect = optarg;
			break;
		case 'u': /* server URL */
			result.config.server_url = optarg;
			break;
		case 'p': /* port */
			if (is_intpos(optarg)) {
				result.config.server_port = atoi(optarg);
			} else {
				usage4(_("Port must be a positive integer"));
			}
			break;
		case 'w': /* warning time threshold */
			if (is_intnonneg(optarg)) {
				result.config.warning_time = atoi(optarg);
				result.config.check_warning_time = true;
			} else {
				usage4(_("Warning time must be a positive integer"));
			}
			break;
		case 'c': /* critical time threshold */
			if (is_intnonneg(optarg)) {
				result.config.critical_time = atoi(optarg);
				result.config.check_critical_time = true;
			} else {
				usage4(_("Critical time must be a positive integer"));
			}
			break;
		case 'v': /* verbose */
			verbose = true;
			break;
		case 't': /* timeout */
			if (is_intnonneg(optarg)) {
				socket_timeout = atoi(optarg);
			} else {
				usage4(_("Timeout interval must be a positive integer"));
			}
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case '?': /* usage */
			usage5();
		}
	}

	int option_char = optind;
	if (result.config.server_address == NULL && argc > option_char) {
		if (is_host(argv[option_char])) {
			result.config.server_address = argv[option_char++];
		} else {
			usage2(_("Invalid hostname/address"), argv[option_char]);
		}
	}

	if (result.config.server_address == NULL) {
		usage4(_("You must provide a server to check"));
	}

	if (result.config.host_name == NULL) {
		result.config.host_name = strdup(result.config.server_address);
	}

	if (result.config.server_expect == NULL) {
		result.config.server_expect = strdup(EXPECT);
	}

	return result;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Pedro Leite <leite@cic.ua.pt>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin tests the REAL service on the specified host."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(" %s\n", "-u, --url=STRING");
	printf("    %s\n", _("Connect to this url"));
	printf(" %s\n", "-e, --expect=STRING");
	printf(_("String to expect in first line of server response (default: %s)\n"), EXPECT);

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("This plugin will attempt to open an RTSP connection with the host."));
	printf("%s\n", _("Successful connects return STATE_OK, refusals and timeouts return"));
	printf("%s\n", _("STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful connects,"));
	printf("%s\n", _("but incorrect response messages from the host result in STATE_WARNING return"));
	printf("%s\n", _("values."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host [-e expect] [-p port] [-w warn] [-c crit] [-t timeout] [-v]\n", progname);
}
