/*
	Copyright (C) 2013 CurlyMo

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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../pilight.h"
#include "common.h"
#include "log.h"
#include "protocol.h"
#include "hardware.h"
#include "relay.h"
#include "gc.h"
#include "wiringPi.h"

void relayCreateMessage(int gpio, int state) {
	relay->message = json_mkobject();
	json_append_member(relay->message, "gpio", json_mknumber(gpio));
	if(state == 1)
		json_append_member(relay->message, "state", json_mkstring("on"));
	else
		json_append_member(relay->message, "state", json_mkstring("off"));
}

int relayCreateCode(JsonNode *code) {
	int gpio = -1;
	int state = -1;
	char *tmp;
	char *hw_mode;
	char *def = NULL;
#ifdef HARDWARE_433_GPIO
	int gpio_in = GPIO_IN_PIN;
	int gpio_out = GPIO_OUT_PIN;
#endif	
	int free_hw_mode = 0;
	int free_def = 0;
	int have_error = 0;

	relay->rawlen = 0;
	if(protocol_setting_get_string(relay, "default", &def) != 0) {
		def = malloc(4);
		free_def = 1;
		strcpy(def, "off");
	}
	
	if(json_find_string(code, "gpio", &tmp) == 0)
		gpio=atoi(tmp);
	if(json_find_string(code, "off", &tmp) == 0)
		state=0;
	else if(json_find_string(code, "on", &tmp) == 0)
		state=1;
	
	if(settings_find_string("hw-mode", &hw_mode) != 0) {
		hw_mode = malloc(strlen(HW_MODE)+1);
		strcpy(hw_mode, HW_MODE);
		free_hw_mode = 1;
	}

#ifdef HARDWARE_433_GPIO
	settings_find_number("gpio-receiver", &gpio_in);
	settings_find_number("gpio-sender", &gpio_out);
#endif
	
	if(gpio == -1 || state == -1) {
		logprintf(LOG_ERR, "relay: insufficient number of arguments");
		have_error = 1;
		goto clear;
	} else if(gpio > 20 || gpio < 0) {
		logprintf(LOG_ERR, "relay: invalid gpio range");
		have_error = 1;
		goto clear;
#ifdef HARDWARE_433_GPIO
	} else if(strstr(progname, "daemon") != 0 && strcmp(hw_mode, "gpio") == 0 && (gpio == gpio_in || gpio == gpio_out)) {
		logprintf(LOG_ERR, "relay: gpio's already in use");
		have_error = 1;
		goto clear;
#endif
	} else {
		if(strstr(progname, "daemon") != 0) {
			if(strcmp(hw_mode, "none") != 0) {
				if(wiringPiSetup() < 0) {
					logprintf(LOG_ERR, "unable to setup wiringPi") ;
					return EXIT_FAILURE;
				} else {
					pinMode(gpio, OUTPUT);
					if(strcmp(def, "off") == 0) {
						if(state == 1) {
							digitalWrite(gpio, LOW);
						} else if(state == 0) {
							digitalWrite(gpio, HIGH);
						}
					} else {
						if(state == 0) {
							digitalWrite(gpio, LOW);
						} else if(state == 1) {
							digitalWrite(gpio, HIGH);
						}
					}
				}
			}
			relayCreateMessage(gpio, state);
		}
		goto clear;
	}

clear:
	if(free_hw_mode) sfree((void *)&hw_mode);
	if(free_def) sfree((void *)&def);
	if(have_error) {
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
}

void relayPrintHelp(void) {
	printf("\t -t --on\t\t\tturn the relay on\n");
	printf("\t -f --off\t\t\tturn the relay off\n");
	printf("\t -g --gpio=gpio\t\t\tthe gpio the relay is connected to\n");
}

void relayInit(void) {

	protocol_register(&relay);
	protocol_set_id(relay, "relay");
	protocol_device_add(relay, "relay", "Control connected relay's");
	relay->devtype = RELAY;

	options_add(&relay->options, 't', "on", no_value, config_state, NULL);
	options_add(&relay->options, 'f', "off", no_value, config_state, NULL);
	options_add(&relay->options, 'g', "gpio", has_value, config_id, "^[0-7]{1}$");

	protocol_setting_add_string(relay, "default", "off");
	protocol_setting_add_string(relay, "states", "on,off");
	protocol_setting_add_number(relay, "readonly", 0);
	
	relay->createCode=&relayCreateCode;
	relay->printHelp=&relayPrintHelp;
}
