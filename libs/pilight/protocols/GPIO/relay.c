/*
	Copyright (C) 2013 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../../core/gc.h"
#include "../protocol.h"
#include "relay.h"

#include "../../../wiringx/wiringX.h"

static char default_state[] = "off";

static void *reason_code_received_free(void *param) {
	struct reason_code_received_t *data = param;
	FREE(data);
	return NULL;
}

static void createMessage(char *message, int gpio, int state) {
	int x = snprintf(message, 255, "{\"gpio\":%d,", gpio);
	if(state == 1)
		x += snprintf(&message[x], 255-x, "\"state\":\"on\"}");
	else
		x += snprintf(&message[x], 255-x, "\"state\":\"off\"}");
}

static int createCode(struct JsonNode *code, char *message) {
	int free_def = 0;
	int gpio = -1;
	int state = -1;
	double itmp = -1;
	char *def = NULL;
	int have_error = 0;

	relay->rawlen = 0;
	if(json_find_string(code, "default-state", &def) != 0) {
		if((def = MALLOC(4)) == NULL) {
			OUT_OF_MEMORY
		}
		strcpy(def, "off");
		free_def = 1;
	}

	if(json_find_number(code, "gpio", &itmp) == 0) {
		gpio = (int)round(itmp);
	}
	if(json_find_number(code, "off", &itmp) == 0) {
		state=0;
	} else if(json_find_number(code, "on", &itmp) == 0) {
		state=1;
	}

	if(gpio == -1 || state == -1) {
		logprintf(LOG_ERR, "relay: insufficient number of arguments");
		have_error = 1;
		goto clear;
	} else if(wiringXSupported() == 0) {
		if(wiringXSetup() < 0) {
			logprintf(LOG_ERR, "unable to setup wiringX") ;
			return EXIT_FAILURE;
		} else {
			if(wiringXValidGPIO(gpio) != 0) {
				logprintf(LOG_ERR, "relay: invalid gpio range");
				have_error = 1;
				goto clear;
			} else {
				if(pilight.process == PROCESS_DAEMON) {
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
				} else {
					wiringXGC();
				}
				createMessage(message, gpio, state);
				goto clear;
			}
		}
	} else {
		createMessage(message, gpio, state);
		goto clear;
	}

clear:
	if(free_def == 1) {
		FREE(def);
	}
	if(have_error) {
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
}

static void printHelp(void) {
	printf("\t -t --on\t\t\tturn the relay on\n");
	printf("\t -f --off\t\t\tturn the relay off\n");
	printf("\t -g --gpio=gpio\t\t\tthe gpio the relay is connected to\n");
}

static int checkValues(struct JsonNode *code) {
	char *def = NULL;
	struct JsonNode *jid = NULL;
	struct JsonNode *jchild = NULL;
	int free_def = 0;
	double itmp = -1;

	if(json_find_string(code, "default-state", &def) != 0) {
		if((def = MALLOC(4)) == NULL) {
			OUT_OF_MEMORY
		}
		strcpy(def, "off");
		free_def = 1;
	}
	if(strcmp(def, "on") != 0 && strcmp(def, "off") != 0) {
		if(free_def == 1) {
			FREE(def);
		}
		return 1;
	}

	/* Get current relay state and validate GPIO number */
	if((jid = json_find_member(code, "id")) != NULL) {
		if((jchild = json_find_element(jid, 0)) != NULL) {
			if(json_find_number(jchild, "gpio", &itmp) == 0) {
				if(wiringXSupported() == 0) {
					int gpio = (int)itmp;
					int state = -1;
					if(wiringXSetup() < 0) {
						logprintf(LOG_ERR, "unable to setup wiringX") ;
						return -1;
					} else if(wiringXValidGPIO(gpio) != 0) {
						logprintf(LOG_ERR, "relay: invalid gpio range");
						return -1;
					} else {
						pinMode(gpio, INPUT);
						state = digitalRead(gpio);
						if(strcmp(def, "on") == 0) {
							state ^= 1;
						}

						struct reason_code_received_t *data = MALLOC(sizeof(struct reason_code_received_t));
						if(data == NULL) {
							OUT_OF_MEMORY
						}
						snprintf(data->message, 1024, "{\"gpio\":%d,\"state\":\"%s\"}", gpio, ((state == 1) ? "on" : "off"));
						strncpy(data->origin, "receiver", 256);
						data->protocol = relay->id;
						if(strlen(pilight_uuid) > 0) {
							data->uuid = pilight_uuid;
						} else {
							data->uuid = NULL;
						}
						data->repeat = 1;
						eventpool_trigger(REASON_CODE_RECEIVED, reason_code_received_free, data);
					}
				}
			}
		}
	}

	if(free_def == 1) {
		FREE(def);
	}
	return 0;
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void relayInit(void) {

	protocol_register(&relay);
	protocol_set_id(relay, "relay");
	protocol_device_add(relay, "relay", "GPIO Connected Relays");
	relay->devtype = RELAY;
	relay->hwtype = HWRELAY;
	relay->multipleId = 0;

	options_add(&relay->options, 't', "on", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&relay->options, 'f', "off", OPTION_NO_VALUE, DEVICES_STATE, JSON_STRING, NULL, NULL);
	options_add(&relay->options, 'g', "gpio", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "[0-9]");

	options_add(&relay->options, 0, "default-state", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_STRING, (void *)default_state, NULL);
	options_add(&relay->options, 0, "readonly", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&relay->options, 0, "confirm", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	relay->checkValues=&checkValues;
	relay->createCode=&createCode;
	relay->printHelp=&printHelp;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "relay";
	module->version = "4.0";
	module->reqversion = "7.0";
	module->reqcommit = "94";
}

void init(void) {
	relayInit();
}
#endif
