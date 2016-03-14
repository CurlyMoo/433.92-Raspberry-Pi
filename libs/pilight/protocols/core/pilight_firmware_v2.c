/*
	Copyright (C) 2014 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "pilight_firmware_v2.h"

#define PULSE_MULTIPLIER	4
#define MIN_PULSE_LENGTH	175
#define MAX_PULSE_LENGTH	225
#define AVG_PULSE_LENGTH	183
#define RAW_LENGTH				196

static int validate(void) {
	if(pilight_firmware_v2->rawlen == RAW_LENGTH) {
		if(pilight_firmware_v2->raw[pilight_firmware_v2->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
		   pilight_firmware_v2->raw[pilight_firmware_v2->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV) &&
			 pilight_firmware_v2->raw[1] > AVG_PULSE_LENGTH*PULSE_MULTIPLIER) {
			return 0;
		}
	}

	return -1;
}

static void parseCode(char *message) {
	int i = 0, x = 0, binary[RAW_LENGTH/4];

	for(i=0;i<pilight_firmware_v2->rawlen;i+=4) {
		if(pilight_firmware_v2->raw[i+3] < 100) {
			pilight_firmware_v2->raw[i+3]*=10;
		}
		if(pilight_firmware_v2->raw[i+3] > AVG_PULSE_LENGTH*(PULSE_MULTIPLIER/2)) {
			binary[x++] = 1;
		} else {
			binary[x++] = 0;
		}
	}

	int version = binToDec(binary, 0, 15);
	int high = binToDec(binary, 16, 31);
	int low = binToDec(binary, 32, 47);

	snprintf(message, 255, "{\"version\":%d,\"lpf\":%d,\"hpf\":%d}", version, high*10, low*10);
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void pilightFirmwareV2Init(void) {

  protocol_register(&pilight_firmware_v2);
  protocol_set_id(pilight_firmware_v2, "pilight_firmware");
  protocol_device_add(pilight_firmware_v2, "pilight_firmware", "pilight filter firmware");
  pilight_firmware_v2->devtype = FIRMWARE;
  pilight_firmware_v2->hwtype = HWINTERNAL;
	pilight_firmware_v2->minrawlen = RAW_LENGTH;
	pilight_firmware_v2->maxrawlen = RAW_LENGTH;
	pilight_firmware_v2->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
	pilight_firmware_v2->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;

  options_add(&pilight_firmware_v2->options, 'v', "version", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^[0-9]+$");
  options_add(&pilight_firmware_v2->options, 'l', "lpf", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^[0-9]+$");
  options_add(&pilight_firmware_v2->options, 'h', "hpf", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "^[0-9]+$");

  pilight_firmware_v2->parseCode=&parseCode;
  pilight_firmware_v2->validate=&validate;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "pilight_firmware";
	module->version = "3.0";
	module->reqversion = "7.0";
	module->reqcommit = "94";
}

void init(void) {
	pilightFirmwareV2Init();
}
#endif
