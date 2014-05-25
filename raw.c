/*
	Copyright (C) 2013 - 2014 CurlyMo

	This file is part of pilight.

    pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

    pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "pilight.h"
#include "common.h"
#include "settings.h"
#include "hardware.h"
#include "log.h"
#include "wiringPi.h"
#include "threads.h"
#include "irq.h"
#include "dso.h"
#include "gc.h"

static unsigned short main_loop = 1;
static pthread_t pth;

int main_gc(void) {
	main_loop = 0;

	log_shell_disable();

	struct conf_hardware_t *tmp_confhw = conf_hardware;
	while(tmp_confhw) {
		if(tmp_confhw->hardware->deinit) {
			tmp_confhw->hardware->deinit();
		}
		tmp_confhw = tmp_confhw->next;
	}

	threads_gc();
	pthread_join(pth, NULL);

	options_gc();
	settings_gc();
	hardware_gc();
	dso_gc();
	log_gc();

	sfree((void *)&progname);

	return EXIT_SUCCESS;
}

void *receive_code(void *param) {
	int duration = 0;

	struct hardware_t *hw = (hardware_t *)param;
	while(main_loop && hw->receive) {
		duration = hw->receive();
		if(duration > 0) {
			printf("%s: %d\n", hw->id, duration);
		}
	};
	return NULL;
}

int main(int argc, char **argv) {

	gc_attach(main_gc);

	/* Catch all exit signals for gc */
	gc_catch();

	log_shell_enable();
	log_file_disable();
	log_level_set(LOG_NOTICE);

	struct options_t *options = NULL;

	char *args = NULL;
	char *hwfile = NULL;
	pid_t pid = 0;

	char settingstmp[] = SETTINGS_FILE;
	settings_set_file(settingstmp);

	if(!(progname = malloc(12))) {
		logprintf(LOG_ERR, "out of memory");
		exit(EXIT_FAILURE);
	}
	strcpy(progname, "pilight-raw");

	options_add(&options, 'H', "help", OPTION_NO_VALUE, 0, JSON_NULL, NULL, NULL);
	options_add(&options, 'V', "version", OPTION_NO_VALUE, 0, JSON_NULL, NULL, NULL);
	options_add(&options, 'F', "settings", OPTION_HAS_VALUE, 0, JSON_NULL, NULL, NULL);

	while (1) {
		int c;
		c = options_parse(&options, argc, argv, 1, &args);
		if(c == -1)
			break;
		if(c == -2)
			c = 'H';
		switch (c) {
			case 'H':
				printf("Usage: %s [options]\n", progname);
				printf("\t -H --help\t\tdisplay usage summary\n");
				printf("\t -V --version\t\tdisplay version\n");
				printf("\t -F --settings\t\tsettings file\n");
				goto close;
			break;
			case 'V':
				printf("%s %s\n", progname, VERSION);
				goto close;
			break;
			case 'F':
				if(settings_set_file(args) == EXIT_FAILURE) {
					goto close;
				}
			break;
			default:
				printf("Usage: %s [options]\n", progname);
				goto close;
			break;
		}
	}
	options_delete(options);

	char pilight_daemon[] = "pilight-daemon";
	char pilight_learn[] = "pilight-learn";
	char pilight_debug[] = "pilight-debug";
	if((pid = findproc(pilight_daemon, NULL, 1)) > 0) {
		logprintf(LOG_ERR, "pilight-daemon instance found (%d)", (int)pid);
		goto close;
	}

	if((pid = findproc(pilight_learn, NULL, 1)) > 0) {
		logprintf(LOG_ERR, "pilight-learn instance found (%d)", (int)pid);
		goto close;
	}

	if((pid = findproc(pilight_debug, NULL, 1)) > 0) {
		logprintf(LOG_ERR, "pilight-debug instance found (%d)", (int)pid);
		goto close;
	}

	if(settings_read() != 0) {
		goto close;
	}

	hardware_init();

	if(settings_find_string("hardware-file", &hwfile) == 0) {
		hardware_set_file(hwfile);
		if(hardware_read() == EXIT_FAILURE) {
			goto close;
		}
	}

	/* Start threads library that keeps track of all threads used */
	threads_create(&pth, NULL, &threads_start, (void *)NULL);

	struct conf_hardware_t *tmp_confhw = conf_hardware;
	while(tmp_confhw) {
		if(tmp_confhw->hardware->init() == EXIT_FAILURE) {
			logprintf(LOG_ERR, "could not initialize %s hardware mode", tmp_confhw->hardware->id);
			goto close;
		}
		threads_register(tmp_confhw->hardware->id, &receive_code, (void *)tmp_confhw->hardware, 0);
		tmp_confhw = tmp_confhw->next;
	}

	while(main_loop) {
		sleep(1);
	}

close:
	main_gc();
	return (EXIT_FAILURE);
}
