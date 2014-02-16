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

#ifndef _PILIGHT_H_
#define _PILIGHT_H_

#include "json.h"

/* Internals */

#define WEBSERVER
/* #undef DEBUG */
#define UPDATE

/* Protocol support */

#define PROTOCOL_ALECTO
#define PROTOCOL_RPI_TEMP
#define PROTOCOL_BRENNENSTUHL_SWITCH
#define PROTOCOL_CLARUS
#define PROTOCOL_COCO_SWITCH
#define PROTOCOL_COGEX_SWITCH
#define PROTOCOL_CONRAD_RSL_SWITCH
#define PROTOCOL_CONRAD_RSL_CONTACT
#define PROTOCOL_DS18B20
#define PROTOCOL_DHT11
#define PROTOCOL_DHT22
#define PROTOCOL_DIO_SWITCH
#define PROTOCOL_GENERIC_DIMMER
#define PROTOCOL_GENERIC_SWITCH
#define PROTOCOL_GENERIC_WEATHER
#define PROTOCOL_HOMEEASY_OLD
#define PROTOCOL_IMPULS
#define PROTOCOL_INTERTECHNO_SWITCH
#define PROTOCOL_INTERTECHNO_OLD
#define PROTOCOL_KAKU_DIMMER
#define PROTOCOL_KAKU_SWITCH_OLD
#define PROTOCOL_KAKU_SCREEN_OLD
#define PROTOCOL_KAKU_SWITCH
#define PROTOCOL_KAKU_SCREEN
#define PROTOCOL_LM75
#define PROTOCOL_NEXA_SWITCH
#define PROTOCOL_RAW
#define PROTOCOL_RELAY
#define PROTOCOL_REV
#define PROTOCOL_ELRO_SWITCH
#define PROTOCOL_SELECTREMOTE
#define PROTOCOL_THREECHAN

/* Hardware support */

#define HARDWARE_433_GPIO
#define HARDWARE_433_LIRC
#define HARDWARE_433_PILIGHT

#define VERSION					"2.1"
#define PULSE_DIV				34

#ifdef WEBSERVER
	#define WEBSERVER_PORT		5001
	#define WEBSERVER_ROOT		"/usr/local/share/pilight/"
	#define WEBSERVER_ENABLE	1
	#define WEBSERVER_CACHE		1
	#define WEBGUI_TEMPLATE		"default"
#endif

#define MAX_CLIENTS				30
#define BUFFER_SIZE				1025
#define BIG_BUFFER_SIZE			1024001

#define PID_FILE				"/var/run/pilight.pid"
#define CONFIG_FILE				"/etc/pilight/config.json"
#define LOG_FILE				"/var/log/pilight.log"
#define SETTINGS_FILE			"/etc/pilight/settings.json"

#define SEND_REPEATS			10
#define RECEIVE_REPEATS			1
#define UUID_LENGTH				21

#ifdef UPDATE
	#define UPDATE_MIRROR			"http://apt.pilight.org/mirror.txt"
	#define UPDATE_CHECK			1
	#define UPDATE_DEVELOPMENT		0
#endif

typedef struct pilight_t {
    void (*broadcast)(char *name, JsonNode *message);
    void (*send)(JsonNode *json);
    void (*receive)(int *rawcode, int rawlen, int plslen, int hwtype);
} pilight_t;

struct pilight_t pilight;

char pilight_uuid[UUID_LENGTH];

#endif
