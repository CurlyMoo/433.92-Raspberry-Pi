/*
	Copyright (C) 2018

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

/*
 *  this implementation is based on RTL_433 project code for HIDEKI
 * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "hideki.h"

#define MIN_PULSE_LENGTH			220
#define MAX_PULSE_LENGTH			280

#define MIN_RAW_LENGTH			130
#define MAX_RAW_LENGTH		180
#define PULSE_HIDEKI_WEATHER_LOWER		300	// experimentally detected
#define PULSE_HIDEKI_WEATHER_UPPER		700	// experimentally detected

#define HIDEKI_MAX_BYTES_PER_ROW 14

//#define HIDEKI_DEBUG

/*
 * The received bits are inverted.
 * Every 8 bits are stuffed with a (even) parity bit.
 * The payload (excluding the header) has an byte parity (XOR) check
 * The payload (excluding the header) has CRC-8, poly 0x07 init 0x00 check
 * The payload bytes are reflected (LSB first / LSB last) after the CRC check
 *
 *    11111001 0  11110101 0  01110011 1 01111010 1  11001100 0  01000011 1  01000110 1  00111111 0  00001001 0  00010111 0
 *    SYNC+HEAD P   RC cha P     LEN   P     Nr.? P   .1° 1°  P   10°  BV P   1%  10% P     ?     P     XOR   P     CRC   P
 *
 * TS04:
 *    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999
 *    SYNC+HEAD cha   RC     LEN        Nr.?    1° .1°  VB   10°   10%  1%     ?         XOR      CRC
 *
 * Wind:
 *    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999 AAAAAAAA BBBBBBBB CCCCCCCC DDDDDDDD
 *    SYNC+HEAD cha   RC     LEN        Nr.?    1° .1°  VB   10°    1° .1°  VB   10°   1W .1W  .1G 10W   10G 1G    w°  AA    XOR      CRC
 *
 * Rain:
 *    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888
 *    SYNC+HEAD cha   RC   B LEN        Nr.?   RAIN_L    RAIN_H     0x66       XOR       CRC
 *
 */

enum sensortypes { HIDEKI_UNKNOWN, HIDEKI_TEMP, HIDEKI_TS04, HIDEKI_WIND, HIDEKI_RAIN };

typedef struct settings_t {
	double rc; // double: this type seems necessary because of the data types expected by JSON
	double channel;
	double temp;
	double humi;
	double wind_dir;
	double wind_factor;
	double rain;
	struct settings_t *next;
} settings_t;

static struct settings_t *settings = NULL;

static uint8_t reverse8(uint8_t x)
{
    x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
    x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
    x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
    return x;
}

static int byteParity(uint8_t inByte)
{
    inByte ^= inByte >> 4;
    inByte &= 0xf;
    return (0x6996 >> inByte) & 1;
}

static void set_bit(uint8_t *array, int position) {
	uint8_t mask = 1 << (7 - (position % 8));
	array[position / 8] |= mask;
}

#if 0
static void reset_bit(uint8_t *array, int position) {
	uint8_t mask = 1 << (7 - (position % 8));
	array[position / 8] &= mask;
}
#endif

static int validate(void) {
	int x = 0, i = 0;
	uint8_t header = 0;

	if ((hideki->rawlen < MIN_RAW_LENGTH) || (hideki->rawlen > MAX_RAW_LENGTH))
	  return -1;

	for(x=0; x<8; x++) {
		if(hideki->raw[i] > PULSE_HIDEKI_WEATHER_LOWER && 	hideki->raw[i] < PULSE_HIDEKI_WEATHER_UPPER) {
				set_bit(&header, x);
				i++;
			} else {
				 // leave value unchanged to 0
			}
			i++;
		}
#ifdef HIDEKI_DEBUG
	logprintf(LOG_DEBUG, "HIDEKI validate(): rawlen=%d, header byte: %02X", hideki->rawlen, header);
#endif

	if (header != 0x06) // tested with TS04 only
        	return -1;

	return 0;
}

static void parseCode(void) {
	uint8_t binary[MAX_RAW_LENGTH/2];
	int id = 0;
	int i = 0, x;
	double humi_offset = 0.0, temp_offset = 0.0, wind_dir_offset = 0.0, wind_factor = 1.0, rain_factor = 1.0;
	uint8_t packet[HIDEKI_MAX_BYTES_PER_ROW];
	int sensortype = HIDEKI_WIND; // default for 14 valid bytes
    uint8_t channel, rc, battery_ok;
    int temp, rain_units;
    double temperature, humidity, wind_strength, wind_direction, rain;

	// Decode Biphase Mark Coded Differential Manchester (BMCDM) pulse stream into binary
	memset(&binary[0], 0, sizeof(binary));
	for(x=0; x<=(MAX_RAW_LENGTH/2); x++) {
		if(hideki->raw[i] > PULSE_HIDEKI_WEATHER_LOWER && hideki->raw[i] < PULSE_HIDEKI_WEATHER_UPPER) {
			set_bit(binary, x);
			i++;
		} else {
			 // leave value unchanged to 0
		}
		i++;
	}
#ifdef HIDEKI_DEBUG
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): raw: %04d %04d %04d %04d  %04d %04d %04d %04d",
			hideki->raw[0], hideki->raw[1], hideki->raw[2], hideki->raw[3], 
			hideki->raw[4], hideki->raw[5], hideki->raw[6], hideki->raw[7]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): raw: %04d %04d %04d %04d  %04d %04d %04d %04d",
			hideki->raw[8], hideki->raw[9], hideki->raw[10], hideki->raw[11], 
			hideki->raw[12], hideki->raw[13], hideki->raw[14], hideki->raw[15]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): raw: %04d %04d %04d %04d  %04d %04d %04d %04d",
			hideki->raw[16], hideki->raw[17], hideki->raw[18], hideki->raw[19], 
			hideki->raw[20], hideki->raw[21], hideki->raw[22], hideki->raw[23]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): raw: %04d %04d %04d %04d  %04d %04d %04d %04d",
			hideki->raw[24], hideki->raw[25], hideki->raw[26], hideki->raw[27], 
			hideki->raw[28], hideki->raw[29], hideki->raw[30], hideki->raw[31]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): raw: %04d %04d %04d %04d  %04d %04d %04d %04d",
			hideki->raw[32], hideki->raw[33], hideki->raw[34], hideki->raw[35], 
			hideki->raw[36], hideki->raw[37], hideki->raw[38], hideki->raw[39]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): raw: %04d %04d %04d %04d  %04d %04d %04d %04d",
			hideki->raw[40], hideki->raw[41], hideki->raw[42], hideki->raw[43], 
			hideki->raw[44], hideki->raw[45], hideki->raw[46], hideki->raw[47]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): bin: %02X %02X %02X %02X %02X %02X %02X %02X",
			binary[0], binary[1], binary[2], binary[3], binary[4], binary[5], binary[6], binary[7]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): bin: %02X %02X %02X %02X %02X %02X %02X %02X",
			binary[8], binary[9], binary[10], binary[11], binary[12], binary[13], binary[14], binary[15]);
#endif

	// Transform incoming data:
	//  * reverse MSB/LSB
	//  * invert all bits
	//  * Remove (and check) parity bit
	for (i = 0; i < HIDEKI_MAX_BYTES_PER_ROW; i++) {
	    unsigned int offset = i/8;
	    packet[i] = (binary[i+offset] << (i%8)) | (binary[i+offset+1] >> (8 - i%8)); // skip/remove parity bit
	    packet[i] = reverse8(packet[i]); // reverse LSB first to LSB last
	    packet[i] ^= 0xFF; // invert bits
	    // check parity
	    uint8_t parity = ((binary[i+offset+1] >> (7 - i%8)) ^ 0xFF) & 0x01;
	    if (parity != byteParity(packet[i]))
	    {
	    	if (i == 10) {
	    		sensortype = HIDEKI_TS04;
	            break;
	         }
	    	if (i == 9) {
	    		sensortype = HIDEKI_RAIN;
	            break;
	        }
	    	if (i == 8) {
	    		sensortype = HIDEKI_TEMP;
	            break;
	        }
#ifdef HIDEKI_DEBUG
			logprintf(LOG_DEBUG, "HIDEKI parseCode(): unsupported sensor type (%d/%d/%d)", i, parity, byteParity(packet[i]));
#endif
			return; // no success
	    }
	}

#ifdef HIDEKI_DEBUG
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): %02X %02X %02X %02X %02X %02X %02X %02X",
			packet[0], packet[1], packet[2], packet[3], packet[4], packet[5], packet[6], packet[7]);
	logprintf(LOG_DEBUG, "HIDEKI parseCode(): %02X %02X %02X %02X %02X %02X %02X %02X",
			packet[8], packet[9], packet[10], packet[11], packet[12], packet[13], packet[14], packet[15]);
#endif

#if 0
    if (packet[0] != 0x9f) // NOTE: other valid ids might exist
        return;
#endif

    // decode the sensor values
     channel = (packet[1] >> 5) & 0x0F;
     if (channel >= 5) channel -= 1;
     rc = packet[1] & 0x0F; // rolling code
     temp = (packet[5] & 0x0F) * 100 + ((packet[4] & 0xF0) >> 4) * 10 + (packet[4] & 0x0F);
     if (((packet[5]>>7) & 0x01) == 0) {
         temp = -temp;
     }
     battery_ok = (packet[5]>>6) & 0x01;
     id = packet[3]; // probably some ID

#ifdef HIDEKI_DEBUG
     logprintf(LOG_DEBUG, "HIDEKI parseCode(): channel %02X, ID:  %02X, RC: %02X, temp: %02X, batt: %02X, sensortyp: %02x, hum: %02X",
			channel, id, rc, temp, battery_ok, sensortype, ((packet[6] & 0xF0) >> 4) * 10 + (packet[6] & 0x0F));
#endif

     // read some settings
 	 struct settings_t *tmp = settings;
 	 while(tmp) {
 		if (fabs(tmp->channel - (double)channel) < EPSILON)  {
 			humi_offset = tmp->humi;
 			temp_offset = tmp->temp;
 			wind_dir_offset = tmp->wind_dir;
 			wind_factor = tmp->wind_factor;
 			rain_factor = tmp->rain;
 			break;
 		}
 		tmp = tmp->next;
 	 }

     if (sensortype == HIDEKI_TS04) {
    	  temperature = (double)temp/10 + temp_offset;
          humidity = ((packet[6] & 0xF0) >> 4) * 10 + (packet[6] & 0x0F) + humi_offset;

          hideki->message = json_mkobject();
          json_append_member(hideki->message, "temperature", json_mknumber(temperature, 2));
          json_append_member(hideki->message, "humidity", json_mknumber(humidity, 2));

          json_append_member(hideki->message, "battery", json_mknumber(battery_ok, 0));
          json_append_member(hideki->message, "channel", json_mknumber(channel, 0));
#if 0
          json_append_member(hideki->message, "id", json_mknumber(id, 0)); // ID value changes: maybe this device has no unique ID
#endif
          json_append_member(hideki->message, "rc", json_mknumber(rc, 0));

      } else  if (sensortype == HIDEKI_WIND) {
          const uint8_t wd[] = { 0, 15, 13, 14, 9, 10, 12, 11, 1, 2, 4, 3, 8, 7, 5, 6 };
    	  temperature = (double)temp/10 + temp_offset;
    	  wind_strength = (double)(packet[9] & 0x0F) * 100 + ((packet[8] & 0xF0) >> 4) * 10 + (packet[8] & 0x0F); // unit still unclear
    	  wind_strength *= wind_factor; // user defined adjustment
    	  // wind strength in km/h = wind_strength*0.160934f
          wind_direction = (double)(wd[((packet[11] & 0xF0) >> 4)] * 225);
          wind_direction /= 10;
          wind_direction += wind_dir_offset; // user defined adjustment
#if 0
          if (wind_direction > 360.0)
        	  wind_direction -= 360.0;
#endif
          hideki->message = json_mkobject();
          json_append_member(hideki->message, "temperature", json_mknumber(temperature, 2));
          json_append_member(hideki->message, "wind strength", json_mknumber(wind_strength, 2));
          json_append_member(hideki->message, "wind direction", json_mknumber(wind_direction, 2));

          json_append_member(hideki->message, "battery", json_mknumber(battery_ok, 0));
          json_append_member(hideki->message, "channel", json_mknumber(channel, 0));
          json_append_member(hideki->message, "id", json_mknumber(id, 0));
          json_append_member(hideki->message, "rc", json_mknumber(rc, 0));

      } else if (sensortype == HIDEKI_TEMP) {
    	 temperature = (double)temp/10 + temp_offset;

         hideki->message = json_mkobject();
         json_append_member(hideki->message, "temperature", json_mknumber(temperature, 2));

         json_append_member(hideki->message, "battery", json_mknumber(battery_ok, 0));
         json_append_member(hideki->message, "channel", json_mknumber(channel, 0));
         json_append_member(hideki->message, "id", json_mknumber(id, 0));
         json_append_member(hideki->message, "rc", json_mknumber(rc, 0));

     } else if (sensortype == HIDEKI_RAIN) {
         rain_units = (packet[5] << 8) + packet[4];
         rain = (double)rain_units * 0.7; // in mm
         rain *= rain_factor;	// user defined adjustment
         battery_ok = (packet[2]>>6) & 0x01;

         hideki->message = json_mkobject();
         json_append_member(hideki->message, "rain", json_mknumber(rain, 2));

         json_append_member(hideki->message, "battery", json_mknumber(battery_ok, 0));
         json_append_member(hideki->message, "channel", json_mknumber(channel, 0));
         json_append_member(hideki->message, "id", json_mknumber(id, 0));
         json_append_member(hideki->message, "rc", json_mknumber(rc, 0));
     }
}

static int checkValues(struct JsonNode *jvalues) {
	struct JsonNode *jid = NULL;

#ifdef HIDEKI_DEBUG
	logprintf(LOG_DEBUG, "HIDEKI checkValues() called");
#endif

	if((jid = json_find_member(jvalues, "id"))) {
		struct settings_t *snode = NULL;
		struct JsonNode *jchild = NULL;
		struct JsonNode *jchild1 = NULL;
		double channel = -1.0, rc = -1.0;
		int match = 0;

		jchild = json_first_child(jid);
		while(jchild) {
			jchild1 = json_first_child(jchild);
			while(jchild1) {
				if(strcmp(jchild1->key, "channel") == 0) {
					channel = jchild1->number_;
				}
				if(strcmp(jchild1->key, "rc") == 0) {
					rc =  jchild1->number_;
				}
				jchild1 = jchild1->next;
			}
			jchild = jchild->next;
		}

		struct settings_t *tmp = settings;
		while(tmp) {
			if ( /*  fabs(tmp->rc - rc) < EPSILON) &&  */ fabs(tmp->channel - channel) < EPSILON)  {
				match = 1;
#ifdef HIDEKI_DEBUG
				logprintf(LOG_DEBUG, "HIDEKI checkValues(): match found");
#endif
				break;
			}
			tmp = tmp->next;
		}

		if(match == 0) {
			if((snode = MALLOC(sizeof(struct settings_t))) == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(EXIT_FAILURE);
			}
			snode->rc = rc;  // rolling code might be misused as key, iff its value is fix for a device
			snode->channel = channel;
			snode->temp = 0;
			snode->humi = 0;
			snode->wind_dir = 0;
			snode->wind_factor = 1.0;;
			snode->rain = 1.0;

			json_find_number(jvalues, "temperature-offset", &snode->temp);
			json_find_number(jvalues, "humidity-offset", &snode->humi);
			json_find_number(jvalues, "wind-direction-offset", &snode->wind_dir);
			json_find_number(jvalues, "wind-factor", &snode->wind_factor);
			json_find_number(jvalues, "rain-factor", &snode->rain);

			snode->next = settings;
			settings = snode;
#ifdef HIDEKI_DEBUG
			logprintf(LOG_DEBUG, "HIDEKI checkValues(): new setting appended");
#endif
		}
	}
	return 0;
}

static void gc(void) {
	struct settings_t *tmp = NULL;
	while(settings) {
		tmp = settings;
		settings = settings->next;
		FREE(tmp);
	}
	if(settings != NULL) {
		FREE(settings);
	}
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void hidekiInit(void) {
	protocol_register(&hideki);
	protocol_set_id(hideki, "hideki");
	protocol_device_add(hideki, "TS04", "HIDEKI TS04 sensor");
	protocol_device_add(hideki, "Hideki-Wind", "HIDEKI Wind sensor");
	protocol_device_add(hideki, "Hideki-Temp", "HIDEKI Temperature sensor");
	protocol_device_add(hideki, "Hideki-Rain", "HIDEKI Rain sensor");
	hideki->devtype = WEATHER;
	hideki->hwtype = RF433;
	hideki->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
	hideki->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;
	hideki->minrawlen = MIN_RAW_LENGTH;
	hideki->maxrawlen = MAX_RAW_LENGTH;

	// device values
	options_add(&hideki->options, "t", "temperature", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,3}$");
	options_add(&hideki->options, "h", "humidity", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,5}$");
	options_add(&hideki->options, "w", "wind", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,5}$");
	options_add(&hideki->options, "d", "wind_direction", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,5}$");
	options_add(&hideki->options, "r", "rain", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,5}$");
	options_add(&hideki->options, "b", "battery", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[01]$");
	options_add(&hideki->options, "o", "rc", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "[0-9]"); // rolling code, might be used as ID, if constant
	// device IDs
#if 0
	options_add(&hideki->options, "i", "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "[0-9]");
#endif
	options_add(&hideki->options, "c", "channel", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "[0-9]");
	// device settings:
	options_add(&hideki->options, "0", "temperature-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
	options_add(&hideki->options, "0", "humidity-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
	options_add(&hideki->options, "0", "wind-direction-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
	options_add(&hideki->options, "0", "wind-factor", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)1, "^[0-9]{1,5}$");
	options_add(&hideki->options, "0", "rain-factor", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)1, "^[0-9]{1,5}$");
	// GUI  settings:
	options_add(&hideki->options, "0", "temperature-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "[0-9]");
	options_add(&hideki->options, "0", "humidity-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
	options_add(&hideki->options, "0", "wind-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "[0-9]");
	options_add(&hideki->options, "0", "wind_direction-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
	options_add(&hideki->options, "0", "rain-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "[0-9]");
	options_add(&hideki->options, "0", "show-humidity", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
	options_add(&hideki->options, "0", "show-temperature", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
	options_add(&hideki->options, "0", "show-battery", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
	options_add(&hideki->options, "0", "show-wind", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&hideki->options, "0", "show-wind_direction", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&hideki->options, "0", "show-rain", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");
	options_add(&hideki->options, "0", "show-rc", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)0, "^[10]{1}$");

	hideki->parseCode=&parseCode;
	hideki->checkValues=&checkValues;
	hideki->validate=&validate;
	hideki->gc=&gc;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "hideki";
	module->version = "0.2";
	module->reqversion = "1.0";
	module->reqcommit = "85";
}

void init(void) {
	hidekiInit();
}
#endif
