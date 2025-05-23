/*****************************************************************************
 *
 * Monitoring check_nt plugin
 *
 * License: GPL
 * Copyright (c) 2000-2002 Yves Rubin (rubiyz@yahoo.com)
 * Copyright (c) 2003-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_nt plugin
 *
 * This plugin collects data from the NSClient service running on a
 * Windows NT/2000/XP/2003 server.
 * This plugin requires NSClient software to run on NT
 * (https://nsclient.org/)
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

const char *progname = "check_nt";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "check_nt.d/config.h"

enum {
	MAX_VALUE_LIST = 30,
};

static char recv_buffer[MAX_INPUT_BUFFER];

static void fetch_data(const char *address, int port, const char *sendb);

typedef struct {
	int errorcode;
	check_nt_config config;
} check_nt_config_wrapper;
static check_nt_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static void preparelist(char *string);
static bool strtoularray(unsigned long *array, char *string, const char *delim);
static void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_nt_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_nt_config config = tmp_config.config;

	/* initialize alarm signal handling */
	signal(SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm(socket_timeout);

	int return_code = STATE_UNKNOWN;
	char *send_buffer = NULL;
	char *output_message = NULL;
	char *perfdata = NULL;
	char *temp_string = NULL;
	char *temp_string_perf = NULL;
	char *description = NULL;
	char *counter_unit = NULL;
	char *errcvt = NULL;
	unsigned long lvalue_list[MAX_VALUE_LIST];
	switch (config.vars_to_check) {
	case CHECK_CLIENTVERSION:
		xasprintf(&send_buffer, "%s&1", config.req_password);
		fetch_data(config.server_address, config.server_port, send_buffer);
		if (config.value_list != NULL && strcmp(recv_buffer, config.value_list) != 0) {
			xasprintf(&output_message, _("Wrong client version - running: %s, required: %s"), recv_buffer, config.value_list);
			return_code = STATE_WARNING;
		} else {
			xasprintf(&output_message, "%s", recv_buffer);
			return_code = STATE_OK;
		}
		break;
	case CHECK_CPULOAD:
		if (config.value_list == NULL) {
			output_message = strdup(_("missing -l parameters"));
		} else if (!strtoularray(lvalue_list, config.value_list, ",")) {
			output_message = strdup(_("wrong -l parameter."));
		} else {
			/* -l parameters is present with only integers */
			return_code = STATE_OK;
			temp_string = strdup(_("CPU Load"));
			temp_string_perf = strdup(" ");

			/* loop until one of the parameters is wrong or not present */
			int offset = 0;
			while (lvalue_list[0 + offset] > (unsigned long)0 && lvalue_list[0 + offset] <= (unsigned long)17280 &&
				   lvalue_list[1 + offset] > (unsigned long)0 && lvalue_list[1 + offset] <= (unsigned long)100 &&
				   lvalue_list[2 + offset] > (unsigned long)0 && lvalue_list[2 + offset] <= (unsigned long)100) {

				/* Send request and retrieve data */
				xasprintf(&send_buffer, "%s&2&%lu", config.req_password, lvalue_list[0 + offset]);
				fetch_data(config.server_address, config.server_port, send_buffer);

				unsigned long utilization = strtoul(recv_buffer, NULL, 10);

				/* Check if any of the request is in a warning or critical state */
				if (utilization >= lvalue_list[2 + offset]) {
					return_code = STATE_CRITICAL;
				} else if (utilization >= lvalue_list[1 + offset] && return_code < STATE_WARNING) {
					return_code = STATE_WARNING;
				}

				xasprintf(&output_message, _(" %lu%% (%lu min average)"), utilization, lvalue_list[0 + offset]);
				xasprintf(&temp_string, "%s%s", temp_string, output_message);
				xasprintf(&perfdata, _(" '%lu min avg Load'=%lu%%;%lu;%lu;0;100"), lvalue_list[0 + offset], utilization,
						  lvalue_list[1 + offset], lvalue_list[2 + offset]);
				xasprintf(&temp_string_perf, "%s%s", temp_string_perf, perfdata);
				offset += 3; /* move across the array */
			}

			if (strlen(temp_string) > 10) { /* we had at least one loop */
				output_message = strdup(temp_string);
				perfdata = temp_string_perf;
			} else {
				output_message = strdup(_("not enough values for -l parameters"));
			}
		}
		break;
	case CHECK_UPTIME: {
		char *tmp_value_list = config.value_list;
		if (config.value_list == NULL) {
			tmp_value_list = "minutes";
		}
		if (strncmp(tmp_value_list, "seconds", strlen("seconds") + 1) && strncmp(tmp_value_list, "minutes", strlen("minutes") + 1) &&
			strncmp(config.value_list, "hours", strlen("hours") + 1) && strncmp(tmp_value_list, "days", strlen("days") + 1)) {

			output_message = strdup(_("wrong -l argument"));
		} else {
			xasprintf(&send_buffer, "%s&3", config.req_password);
			fetch_data(config.server_address, config.server_port, send_buffer);
			unsigned long uptime = strtoul(recv_buffer, NULL, 10);
			int updays = uptime / 86400;
			int uphours = (uptime % 86400) / 3600;
			int upminutes = ((uptime % 86400) % 3600) / 60;

			if (!strncmp(tmp_value_list, "minutes", strlen("minutes"))) {
				uptime = uptime / 60;
			} else if (!strncmp(tmp_value_list, "hours", strlen("hours"))) {
				uptime = uptime / 3600;
			} else if (!strncmp(tmp_value_list, "days", strlen("days"))) {
				uptime = uptime / 86400;
			}
			/* else uptime in seconds, nothing to do */

			xasprintf(&output_message, _("System Uptime - %u day(s) %u hour(s) %u minute(s) |uptime=%lu"), updays, uphours, upminutes,
					  uptime);

			if (config.check_critical_value && uptime <= config.critical_value) {
				return_code = STATE_CRITICAL;
			} else if (config.check_warning_value && uptime <= config.warning_value) {
				return_code = STATE_WARNING;
			} else {
				return_code = STATE_OK;
			}
		}
	} break;
	case CHECK_USEDDISKSPACE:
		if (config.value_list == NULL) {
			output_message = strdup(_("missing -l parameters"));
		} else if (strlen(config.value_list) != 1) {
			output_message = strdup(_("wrong -l argument"));
		} else {
			xasprintf(&send_buffer, "%s&4&%s", config.req_password, config.value_list);
			fetch_data(config.server_address, config.server_port, send_buffer);
			char *fds = strtok(recv_buffer, "&");
			char *tds = strtok(NULL, "&");
			double total_disk_space = 0;
			double free_disk_space = 0;
			if (fds != NULL) {
				free_disk_space = atof(fds);
			}
			if (tds != NULL) {
				total_disk_space = atof(tds);
			}

			if (total_disk_space > 0 && free_disk_space >= 0) {
				double percent_used_space = ((total_disk_space - free_disk_space) / total_disk_space) * 100;
				double warning_used_space = ((float)config.warning_value / 100) * total_disk_space;
				double critical_used_space = ((float)config.critical_value / 100) * total_disk_space;

				xasprintf(&temp_string, _("%s:\\ - total: %.2f Gb - used: %.2f Gb (%.0f%%) - free %.2f Gb (%.0f%%)"), config.value_list,
						  total_disk_space / 1073741824, (total_disk_space - free_disk_space) / 1073741824, percent_used_space,
						  free_disk_space / 1073741824, (free_disk_space / total_disk_space) * 100);
				xasprintf(&temp_string_perf, _("'%s:\\ Used Space'=%.2fGb;%.2f;%.2f;0.00;%.2f"), config.value_list,
						  (total_disk_space - free_disk_space) / 1073741824, warning_used_space / 1073741824,
						  critical_used_space / 1073741824, total_disk_space / 1073741824);

				if (config.check_critical_value && percent_used_space >= config.critical_value) {
					return_code = STATE_CRITICAL;
				} else if (config.check_warning_value && percent_used_space >= config.warning_value) {
					return_code = STATE_WARNING;
				} else {
					return_code = STATE_OK;
				}

				output_message = strdup(temp_string);
				perfdata = temp_string_perf;
			} else {
				output_message = strdup(_("Free disk space : Invalid drive"));
				return_code = STATE_UNKNOWN;
			}
		}
		break;
	case CHECK_SERVICESTATE:
	case CHECK_PROCSTATE:
		if (config.value_list == NULL) {
			output_message = strdup(_("No service/process specified"));
		} else {
			preparelist(config.value_list); /* replace , between services with & to send the request */
			xasprintf(&send_buffer, "%s&%u&%s&%s", config.req_password, (config.vars_to_check == CHECK_SERVICESTATE) ? 5 : 6,
					  (config.show_all) ? "ShowAll" : "ShowFail", config.value_list);
			fetch_data(config.server_address, config.server_port, send_buffer);
			char *numstr = strtok(recv_buffer, "&");
			if (numstr == NULL) {
				die(STATE_UNKNOWN, _("could not fetch information from server\n"));
			}
			return_code = atoi(numstr);
			temp_string = strtok(NULL, "&");
			output_message = strdup(temp_string);
		}
		break;
	case CHECK_MEMUSE:
		xasprintf(&send_buffer, "%s&7", config.req_password);
		fetch_data(config.server_address, config.server_port, send_buffer);
		char *numstr = strtok(recv_buffer, "&");
		if (numstr == NULL) {
			die(STATE_UNKNOWN, _("could not fetch information from server\n"));
		}
		double mem_commitLimit = atof(numstr);
		numstr = strtok(NULL, "&");
		if (numstr == NULL) {
			die(STATE_UNKNOWN, _("could not fetch information from server\n"));
		}
		double mem_commitByte = atof(numstr);
		double percent_used_space = (mem_commitByte / mem_commitLimit) * 100;
		double warning_used_space = ((float)config.warning_value / 100) * mem_commitLimit;
		double critical_used_space = ((float)config.critical_value / 100) * mem_commitLimit;

		/* Divisor should be 1048567, not 3044515, as we are measuring "Commit Charge" here,
		which equals RAM + Pagefiles. */
		xasprintf(&output_message, _("Memory usage: total:%.2f MB - used: %.2f MB (%.0f%%) - free: %.2f MB (%.0f%%)"),
				  mem_commitLimit / 1048567, mem_commitByte / 1048567, percent_used_space, (mem_commitLimit - mem_commitByte) / 1048567,
				  (mem_commitLimit - mem_commitByte) / mem_commitLimit * 100);
		xasprintf(&perfdata, _("'Memory usage'=%.2fMB;%.2f;%.2f;0.00;%.2f"), mem_commitByte / 1048567, warning_used_space / 1048567,
				  critical_used_space / 1048567, mem_commitLimit / 1048567);

		return_code = STATE_OK;
		if (config.check_critical_value && percent_used_space >= config.critical_value) {
			return_code = STATE_CRITICAL;
		} else if (config.check_warning_value && percent_used_space >= config.warning_value) {
			return_code = STATE_WARNING;
		}

		break;
	case CHECK_COUNTER: {
		/*
		CHECK_COUNTER has been modified to provide extensive perfdata information.
		In order to do this, some modifications have been done to the code
		and some constraints have been introduced.

		1) For the sake of simplicity of the code, perfdata information will only be
		 provided when the "description" field is added.

		2) If the counter you're going to measure is percent-based, the code will detect
		 the percent sign in its name and will attribute minimum (0%) and maximum (100%)
		 values automagically, as well the "%" sign to graph units.

		3) OTOH, if the counter is "absolute", you'll have to provide the following
		 the counter unit - that is, the dimensions of the counter you're getting. Examples:
		 pages/s, packets transferred, etc.

		4) If you want, you may provide the minimum and maximum values to expect. They aren't mandatory,
		 but once specified they MUST have the same order of magnitude and units of -w and -c; otherwise.
		 strange things will happen when you make graphs of your data.
		*/

		double counter_value = 0.0;
		if (config.value_list == NULL) {
			output_message = strdup(_("No counter specified"));
		} else {
			preparelist(config.value_list); /* replace , between services with & to send the request */
			bool isPercent = (strchr(config.value_list, '%') != NULL);

			strtok(config.value_list, "&"); /* burn the first parameters */
			description = strtok(NULL, "&");
			counter_unit = strtok(NULL, "&");
			xasprintf(&send_buffer, "%s&8&%s", config.req_password, config.value_list);
			fetch_data(config.server_address, config.server_port, send_buffer);
			counter_value = atof(recv_buffer);

			bool allRight = false;
			if (description == NULL) {
				xasprintf(&output_message, "%.f", counter_value);
			} else if (isPercent) {
				counter_unit = strdup("%");
				allRight = true;
			}

			char *minval = NULL;
			char *maxval = NULL;
			double fminval = 0;
			double fmaxval = 0;
			if ((counter_unit != NULL) && (!allRight)) {
				minval = strtok(NULL, "&");
				maxval = strtok(NULL, "&");

				/* All parameters specified. Let's check the numbers */

				fminval = (minval != NULL) ? strtod(minval, &errcvt) : -1;
				fmaxval = (minval != NULL) ? strtod(maxval, &errcvt) : -1;

				if ((fminval == 0) && (minval == errcvt)) {
					output_message = strdup(_("Minimum value contains non-numbers"));
				} else {
					if ((fmaxval == 0) && (maxval == errcvt)) {
						output_message = strdup(_("Maximum value contains non-numbers"));
					} else {
						allRight = true; /* Everything is OK. */
					}
				}
			} else if ((counter_unit == NULL) && (description != NULL)) {
				output_message = strdup(_("No unit counter specified"));
			}

			if (allRight) {
				/* Let's format the output string, finally... */
				if (strstr(description, "%") == NULL) {
					xasprintf(&output_message, "%s = %.2f %s", description, counter_value, counter_unit);
				} else {
					/* has formatting, will segv if wrong */
					xasprintf(&output_message, description, counter_value);
				}
				xasprintf(&output_message, "%s |", output_message);
				xasprintf(&output_message, "%s %s", output_message,
						  fperfdata(description, counter_value, counter_unit, 1, config.warning_value, 1, config.critical_value,
									(!(isPercent) && (minval != NULL)), fminval, (!(isPercent) && (minval != NULL)), fmaxval));
			}
		}

		if (config.critical_value > config.warning_value) { /* Normal thresholds */
			if (config.check_critical_value && counter_value >= config.critical_value) {
				return_code = STATE_CRITICAL;
			} else if (config.check_warning_value && counter_value >= config.warning_value) {
				return_code = STATE_WARNING;
			} else {
				return_code = STATE_OK;
			}
		} else { /* inverse thresholds */
			return_code = STATE_OK;
			if (config.check_critical_value && counter_value <= config.critical_value) {
				return_code = STATE_CRITICAL;
			} else if (config.check_warning_value && counter_value <= config.warning_value) {
				return_code = STATE_WARNING;
			}
		}
	} break;
	case CHECK_FILEAGE:
		if (config.value_list == NULL) {
			output_message = strdup(_("No counter specified"));
		} else {
			preparelist(config.value_list); /* replace , between services with & to send the request */
			xasprintf(&send_buffer, "%s&9&%s", config.req_password, config.value_list);
			fetch_data(config.server_address, config.server_port, send_buffer);
			unsigned long age_in_minutes = atoi(strtok(recv_buffer, "&"));
			description = strtok(NULL, "&");
			output_message = strdup(description);

			if (config.critical_value > config.warning_value) { /* Normal thresholds */
				if (config.check_critical_value && age_in_minutes >= config.critical_value) {
					return_code = STATE_CRITICAL;
				} else if (config.check_warning_value && age_in_minutes >= config.warning_value) {
					return_code = STATE_WARNING;
				} else {
					return_code = STATE_OK;
				}
			} else { /* inverse thresholds */
				if (config.check_critical_value && age_in_minutes <= config.critical_value) {
					return_code = STATE_CRITICAL;
				} else if (config.check_warning_value && age_in_minutes <= config.warning_value) {
					return_code = STATE_WARNING;
				} else {
					return_code = STATE_OK;
				}
			}
		}
		break;

	case CHECK_INSTANCES:
		if (config.value_list == NULL) {
			output_message = strdup(_("No counter specified"));
		} else {
			xasprintf(&send_buffer, "%s&10&%s", config.req_password, config.value_list);
			fetch_data(config.server_address, config.server_port, send_buffer);
			if (!strncmp(recv_buffer, "ERROR", 5)) {
				printf("NSClient - %s\n", recv_buffer);
				exit(STATE_UNKNOWN);
			}
			xasprintf(&output_message, "%s", recv_buffer);
			return_code = STATE_OK;
		}
		break;

	case CHECK_NONE:
	default:
		usage4(_("Please specify a variable to check"));
		break;
	}

	/* reset timeout */
	alarm(0);

	if (perfdata == NULL) {
		printf("%s\n", output_message);
	} else {
		printf("%s | %s\n", output_message, perfdata);
	}
	return return_code;
}

/* process command-line arguments */
check_nt_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"port", required_argument, 0, 'p'},
									   {"timeout", required_argument, 0, 't'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"variable", required_argument, 0, 'v'},
									   {"hostname", required_argument, 0, 'H'},
									   {"params", required_argument, 0, 'l'},
									   {"secret", required_argument, 0, 's'},
									   {"display", required_argument, 0, 'd'},
									   {"unknown-timeout", no_argument, 0, 'u'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {0, 0, 0, 0}};

	check_nt_config_wrapper result = {
		.errorcode = OK,
		.config = check_nt_config_init(),
	};

	/* no options were supplied */
	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	/* backwards compatibility */
	if (!is_option(argv[1])) {
		result.config.server_address = strdup(argv[1]);
		argv[1] = argv[0];
		argv = &argv[1];
		argc--;
	}

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		} else if (strcmp("-wv", argv[index]) == 0) {
			strcpy(argv[index], "-w");
		} else if (strcmp("-cv", argv[index]) == 0) {
			strcpy(argv[index], "-c");
		}
	}

	int option = 0;
	while (true) {
		int option_index = getopt_long(argc, argv, "+hVH:t:c:w:p:v:l:s:d:u", longopts, &option);

		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
		case '?': /* print short usage statement if args not parsable */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'H': /* hostname */
			result.config.server_address = optarg;
			break;
		case 's': /* password */
			result.config.req_password = optarg;
			break;
		case 'p': /* port */
			if (is_intnonneg(optarg)) {
				result.config.server_port = atoi(optarg);
			} else {
				die(STATE_UNKNOWN, _("Server port must be an integer\n"));
			}
			break;
		case 'v':
			if (strlen(optarg) < 4) {
				result.errorcode = ERROR;
				return result;
			}
			if (!strcmp(optarg, "CLIENTVERSION")) {
				result.config.vars_to_check = CHECK_CLIENTVERSION;
			} else if (!strcmp(optarg, "CPULOAD")) {
				result.config.vars_to_check = CHECK_CPULOAD;
			} else if (!strcmp(optarg, "UPTIME")) {
				result.config.vars_to_check = CHECK_UPTIME;
			} else if (!strcmp(optarg, "USEDDISKSPACE")) {
				result.config.vars_to_check = CHECK_USEDDISKSPACE;
			} else if (!strcmp(optarg, "SERVICESTATE")) {
				result.config.vars_to_check = CHECK_SERVICESTATE;
			} else if (!strcmp(optarg, "PROCSTATE")) {
				result.config.vars_to_check = CHECK_PROCSTATE;
			} else if (!strcmp(optarg, "MEMUSE")) {
				result.config.vars_to_check = CHECK_MEMUSE;
			} else if (!strcmp(optarg, "COUNTER")) {
				result.config.vars_to_check = CHECK_COUNTER;
			} else if (!strcmp(optarg, "FILEAGE")) {
				result.config.vars_to_check = CHECK_FILEAGE;
			} else if (!strcmp(optarg, "INSTANCES")) {
				result.config.vars_to_check = CHECK_INSTANCES;
			} else {
				result.errorcode = ERROR;
				return result;
			}
			break;
		case 'l': /* value list */
			result.config.value_list = optarg;
			break;
		case 'w': /* warning threshold */
			result.config.warning_value = strtoul(optarg, NULL, 10);
			result.config.check_warning_value = true;
			break;
		case 'c': /* critical threshold */
			result.config.critical_value = strtoul(optarg, NULL, 10);
			result.config.check_critical_value = true;
			break;
		case 'd': /* Display select for services */
			if (!strcmp(optarg, "SHOWALL")) {
				result.config.show_all = true;
			}
			break;
		case 'u':
			socket_timeout_state = STATE_UNKNOWN;
			break;
		case 't': /* timeout */
			socket_timeout = atoi(optarg);
			if (socket_timeout <= 0) {
				result.errorcode = ERROR;
				return result;
			}
		}
	}
	if (result.config.server_address == NULL) {
		usage4(_("You must provide a server address or host name"));
	}

	if (result.config.vars_to_check == CHECK_NONE) {
		result.errorcode = ERROR;
		return result;
	}

	if (result.config.req_password == NULL) {
		result.config.req_password = strdup(_("None"));
	}

	return result;
}

void fetch_data(const char *address, int port, const char *sendb) {
	int result = process_tcp_request(address, port, sendb, recv_buffer, sizeof(recv_buffer));

	if (result != STATE_OK) {
		die(result, _("could not fetch information from server\n"));
	}

	if (!strncmp(recv_buffer, "ERROR", 5)) {
		die(STATE_UNKNOWN, "NSClient - %s\n", recv_buffer);
	}
}

bool strtoularray(unsigned long *array, char *string, const char *delim) {
	/* split a <delim> delimited string into a long array */
	for (int idx = 0; idx < MAX_VALUE_LIST; idx++) {
		array[idx] = 0;
	}

	int idx = 0;
	for (char *t1 = strtok(string, delim); t1 != NULL; t1 = strtok(NULL, delim)) {
		if (is_numeric(t1) && idx < MAX_VALUE_LIST) {
			array[idx] = strtoul(t1, NULL, 10);
			idx++;
		} else {
			return false;
		}
	}
	return true;
}

void preparelist(char *string) {
	/* Replace all , with & which is the delimiter for the request */
	for (int i = 0; (size_t)i < strlen(string); i++) {
		if (string[i] == ',') {
			string[i] = '&';
		}
	}
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 2000 Yves Rubin (rubiyz@yahoo.com)\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin collects data from the NSClient service running on a"));
	printf("%s\n", _("Windows NT/2000/XP/2003 server."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf("%s\n", _("Options:"));
	printf(" %s\n", "-H, --hostname=HOST");
	printf("   %s\n", _("Name of the host to check"));
	printf(" %s\n", "-p, --port=INTEGER");
	printf("   %s", _("Optional port number (default: "));
	printf("%d)\n", PORT);
	printf(" %s\n", "-s, --secret=<password>");
	printf("   %s\n", _("Password needed for the request"));
	printf(" %s\n", "-w, --warning=INTEGER");
	printf("   %s\n", _("Threshold which will result in a warning status"));
	printf(" %s\n", "-c, --critical=INTEGER");
	printf("   %s\n", _("Threshold which will result in a critical status"));
	printf(" %s\n", "-t, --timeout=INTEGER");
	printf("   %s", _("Seconds before connection attempt times out (default: "));
	printf(" %s\n", "-l, --params=<parameters>");
	printf("   %s", _("Parameters passed to specified check (see below)"));
	printf(" %s\n", "-d, --display={SHOWALL}");
	printf("   %s", _("Display options (currently only SHOWALL works)"));
	printf(" %s\n", "-u, --unknown-timeout");
	printf("   %s", _("Return UNKNOWN on timeouts"));
	printf("%d)\n", DEFAULT_SOCKET_TIMEOUT);
	printf(" %s\n", "-h, --help");
	printf("   %s\n", _("Print this help screen"));
	printf(" %s\n", "-V, --version");
	printf("   %s\n", _("Print version information"));
	printf(" %s\n", "-v, --variable=STRING");
	printf("   %s\n\n", _("Variable to check"));
	printf("%s\n", _("Valid variables are:"));
	printf(" %s", "CLIENTVERSION =");
	printf(" %s\n", _("Get the NSClient version"));
	printf("  %s\n", _("If -l <version> is specified, will return warning if versions differ."));
	printf(" %s\n", "CPULOAD =");
	printf("  %s\n", _("Average CPU load on last x minutes."));
	printf("  %s\n", _("Request a -l parameter with the following syntax:"));
	printf("  %s\n", _("-l <minutes range>,<warning threshold>,<critical threshold>."));
	printf("  %s\n", _("<minute range> should be less than 24*60."));
	printf("  %s\n", _("Thresholds are percentage and up to 10 requests can be done in one shot."));
	printf("  %s\n", "ie: -l 60,90,95,120,90,95");
	printf(" %s\n", "UPTIME =");
	printf("  %s\n", _("Get the uptime of the machine."));
	printf("  %s\n", _("-l <unit> "));
	printf("  %s\n", _("<unit> = seconds, minutes, hours, or days. (default: minutes)"));
	printf("  %s\n", _("Thresholds will use the unit specified above."));
	printf(" %s\n", "USEDDISKSPACE =");
	printf("  %s\n", _("Size and percentage of disk use."));
	printf("  %s\n", _("Request a -l parameter containing the drive letter only."));
	printf("  %s\n", _("Warning and critical thresholds can be specified with -w and -c."));
	printf(" %s\n", "MEMUSE =");
	printf("  %s\n", _("Memory use."));
	printf("  %s\n", _("Warning and critical thresholds can be specified with -w and -c."));
	printf(" %s\n", "SERVICESTATE =");
	printf("  %s\n", _("Check the state of one or several services."));
	printf("  %s\n", _("Request a -l parameters with the following syntax:"));
	printf("  %s\n", _("-l <service1>,<service2>,<service3>,..."));
	printf("  %s\n", _("You can specify -d SHOWALL in case you want to see working services"));
	printf("  %s\n", _("in the returned string."));
	printf(" %s\n", "PROCSTATE =");
	printf("  %s\n", _("Check if one or several process are running."));
	printf("  %s\n", _("Same syntax as SERVICESTATE."));
	printf(" %s\n", "COUNTER =");
	printf("  %s\n", _("Check any performance counter of Windows NT/2000."));
	printf("	%s\n", _("Request a -l parameters with the following syntax:"));
	printf("	%s\n", _("-l \"\\\\<performance object>\\\\counter\",\"<description>"));
	printf("	%s\n", _("The <description> parameter is optional and is given to a printf "));
	printf("  %s\n", _("output command which requires a float parameter."));
	printf("  %s\n", _("If <description> does not include \"%%\", it is used as a label."));
	printf("  %s\n", _("Some examples:"));
	printf("  %s\n", "\"Paging file usage is %%.2f %%%%\"");
	printf("  %s\n", "\"%%.f %%%% paging file used.\"");
	printf(" %s\n", "INSTANCES =");
	printf("  %s\n", _("Check any performance counter object of Windows NT/2000."));
	printf("  %s\n", _("Syntax: check_nt -H <hostname> -p <port> -v INSTANCES -l <counter object>"));
	printf("  %s\n", _("<counter object> is a Windows Perfmon Counter object (eg. Process),"));
	printf("  %s\n", _("if it is two words, it should be enclosed in quotes"));
	printf("  %s\n", _("The returned results will be a comma-separated list of instances on "));
	printf("  %s\n", _(" the selected computer for that object."));
	printf("  %s\n", _("The purpose of this is to be run from command line to determine what instances"));
	printf("  %s\n", _(" are available for monitoring without having to log onto the Windows server"));
	printf("  %s\n", _("  to run Perfmon directly."));
	printf("  %s\n", _("It can also be used in scripts that automatically create the monitoring service"));
	printf("  %s\n", _(" configuration files."));
	printf("  %s\n", _("Some examples:"));
	printf("  %s\n\n", _("check_nt -H 192.168.1.1 -p 1248 -v INSTANCES -l Process"));

	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("- The NSClient service should be running on the server to get any information"));
	printf("   %s\n", "(http://nsclient.ready2run.nl).");
	printf(" %s\n", _("- Critical thresholds should be lower than warning thresholds"));
	printf(" %s\n", _("- Default port 1248 is sometimes in use by other services. The error"));
	printf("   %s\n", _("output when this happens contains \"Cannot map xxxxx to protocol number\"."));
	printf("   %s\n", _("One fix for this is to change the port to something else on check_nt "));
	printf("   %s\n", _("and on the client service it\'s connecting to."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host -v variable [-p port] [-w warning] [-c critical]\n", progname);
	printf("[-l params] [-d SHOWALL] [-u] [-t timeout]\n");
}
