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

#include "../../pilight.h"
#include "common.h"
#include "log.h"
#include "protocol.h"
#include "hardware.h"
#include "binary.h"
#include "gc.h"
#include "home_easy_old.h"

void homeEasyOldCreateMessage(int systemcode, int unitcode, int state, int all) {
	home_easy_old->message = json_mkobject();
	json_append_member(home_easy_old->message, "systemcode", json_mknumber(systemcode));
	if(all == 1) {
		json_append_member(home_easy_old->message, "all", json_mknumber(all));
	} else {
		json_append_member(home_easy_old->message, "unitcode", json_mknumber(unitcode));
	}
	if(state == 0) {
		json_append_member(home_easy_old->message, "state", json_mkstring("on"));
	} else {
		json_append_member(home_easy_old->message, "state", json_mkstring("off"));
	}
}

void homeEasyOldParseBinary(void) {
	int systemcode = 15-binToDecRev(home_easy_old->binary, 1, 4);
	int unitcode = 15-binToDecRev(home_easy_old->binary, 5, 8);
	int all = home_easy_old->binary[9];
	int state = home_easy_old->binary[11];
	int check = home_easy_old->binary[10];
	if(check == 0) {
		homeEasyOldCreateMessage(systemcode, unitcode, state, all);
	}
}

void homeEasyOldCreateHigh(int s, int e) {
	int i;

	for(i=s;i<=e;i+=4) {
		home_easy_old->raw[i]=(home_easy_old->plslen->length);
		home_easy_old->raw[i+1]=(home_easy_old->pulse*home_easy_old->plslen->length);
		home_easy_old->raw[i+2]=(home_easy_old->pulse*home_easy_old->plslen->length);
		home_easy_old->raw[i+3]=(home_easy_old->plslen->length);
	}
}

void homeEasyOldCreateLow(int s, int e) {
	int i;

	for(i=s;i<=e;i+=4) {
		home_easy_old->raw[i]=(home_easy_old->plslen->length);
		home_easy_old->raw[i+1]=(home_easy_old->pulse*home_easy_old->plslen->length);
		home_easy_old->raw[i+2]=(home_easy_old->plslen->length);
		home_easy_old->raw[i+3]=(home_easy_old->pulse*home_easy_old->plslen->length);
	}
}

void homeEasyOldClearCode(void) {
	homeEasyOldCreateHigh(0,47);
}

void homeEasyOldCreateStart(void) {
	homeEasyOldCreateLow(0,3);
}

void homeEasyOldCreateSystemCode(int systemcode) {
	int binary[255];
	int length = 0;
	int i=0, x=0;

	length = decToBin(systemcode, binary);
	for(i=0;i<=length;i++) {
		if(binary[i]==1) {
			x=i*4;
			homeEasyOldCreateHigh(20+x, 20+x+3);
		}
	}
}

void homeEasyOldCreateUnitCode(int unitcode) {
	int binary[255];
	int length = 0;
	int i=0, x=0;

	length = decToBin(unitcode, binary);
	for(i=0;i<=length;i++) {
		if(binary[i]==1) {
			x=i*4;
			homeEasyOldCreateHigh(4+x, 4+x+3);
		}
	}
}

void homeEasyOldCreateAll(int all) {
	if(all == 1) {
		homeEasyOldCreateHigh(36, 39);
	}
}

void homeEasyOldCreateState(int state) {
	if(state == 0) {
		homeEasyOldCreateHigh(44, 47);
	}
}

void homeEasyOldCreateFooter(void) {
	home_easy_old->raw[48]=(home_easy_old->plslen->length);
	home_easy_old->raw[49]=(PULSE_DIV*home_easy_old->plslen->length);
}

int homeEasyOldCreateCode(JsonNode *code) {
	int systemcode = -1;
	int unitcode = -1;
	int state = -1;
	int all = -1;
	char *tmp;

	if(json_find_string(code, "systemcode", &tmp) == 0) {
		systemcode = atoi(tmp);
	}
	if(json_find_string(code, "unitcode", &tmp) == 0) {
		unitcode = atoi(tmp);
	}
	if(json_find_string(code, "off", &tmp) == 0) {
		state=1;
	} else if(json_find_string(code, "on", &tmp) == 0) {
		state=0;
	}
	if(json_find_string(code, "all", &tmp) == 0)
		all = 1;	

	if(systemcode == -1 || (unitcode == -1 && all == 0) || state == -1) {
		logprintf(LOG_ERR, "home_easy_old: insufficient number of arguments");
		return EXIT_FAILURE;
	} else if(systemcode > 15 || systemcode < 0) {
		logprintf(LOG_ERR, "home_easy_old: invalid systemcode range");
		return EXIT_FAILURE;
	} else if((unitcode > 15 || unitcode < 0) && all == 0) {
		logprintf(LOG_ERR, "arctech_switch: invalid unit range");
		return EXIT_FAILURE;
	} else {
		if(unitcode == -1 && all == 1) {
			unitcode = 15;
		}	
		homeEasyOldCreateMessage(systemcode, unitcode, state, all);
		homeEasyOldClearCode();
		homeEasyOldCreateStart();
		homeEasyOldCreateSystemCode(systemcode);
		homeEasyOldCreateUnitCode(unitcode);
		homeEasyOldCreateAll(all);
		homeEasyOldCreateState(state);
		homeEasyOldCreateFooter();
	}
	return EXIT_SUCCESS;
}

void homeEasyOldPrintHelp(void) {
	printf("\t -s --systemcode=systemcode\tcontrol a device with this systemcode\n");
	printf("\t -u --unitcode=unitcode\t\tcontrol a device with this unitcode\n");
	printf("\t -a --all\t\t\tsend command to all devices with this id\n");
	printf("\t -t --on\t\t\tsend an on signal\n");
	printf("\t -f --off\t\t\tsend an off signal\n");
}

void homeEasyOldInit(void) {

	protocol_register(&home_easy_old);
	protocol_set_id(home_easy_old, "home_easy_old");
	protocol_device_add(home_easy_old, "home_easy_old", "Old Home Easy Switches");
	protocol_plslen_add(home_easy_old, 289);
	home_easy_old->devtype = SWITCH;
	home_easy_old->hwtype = RX433;
	home_easy_old->pulse = 3;
	home_easy_old->rawlen = 50;
	home_easy_old->binlen = 12;
	home_easy_old->lsb = 3;

	options_add(&home_easy_old->options, 's', "systemcode", has_value, config_id, "^(3[012]?|[012][0-9]|[0-9]{1})$");
	options_add(&home_easy_old->options, 'u', "unitcode", has_value, config_id, "^(3[012]?|[012][0-9]|[0-9]{1})$");
	options_add(&home_easy_old->options, 'a', "all", no_value, config_state, NULL);
	options_add(&home_easy_old->options, 't', "on", no_value, config_state, NULL);
	options_add(&home_easy_old->options, 'f', "off", no_value, config_state, NULL);

	protocol_setting_add_string(home_easy_old, "states", "on,off");	
	protocol_setting_add_number(home_easy_old, "readonly", 0);
	
	home_easy_old->parseBinary=&homeEasyOldParseBinary;
	home_easy_old->createCode=&homeEasyOldCreateCode;
	home_easy_old->printHelp=&homeEasyOldPrintHelp;
}
