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
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "../../pilight.h"
#include "common.h"
#include "log.h"
#include "gc.h"
#include "hardware.h"
#include "json.h"
#include "wiringPi.h"
#include "433pilight.h"

#define IOCTL_GPIO_IN				10
#define IOCTL_LONGEST_V_P			11
#define IOCTL_SHORTEST_V_P			12
#define IOCTL_START_RECEIVER		13
#define IOCTL_STOP_RECEIVER			14
#define IOCTL_FILTER_ON				15
#define IOCTL_GPIO_OUT				16
#define IOCTL_START_TRANSMITTER		17
#define IOCTL_STOP_TRANSMITTER		18

int pilight_433_rec_initialized = 0;
int pilight_433_trans_initialized = 0;
int pilight_433_fd_rec = 0;
int pilight_433_fd_trans = 0;
int pilight_433_out = 0;
int pilight_433_in = 0;
int pilight_433_prefilter = 0;
int pilight_433_svp = 0;
int pilight_433_lvp = 0;
char *pilight_433_socket = NULL;

unsigned short pilight433HwInit(void) {
	int filter_on = 1;

	if(pilight_433_prefilter == 1){
		filter_on = 0;
	}

	int pin_in = wiringPiGetPin(pilight_433_in);
	int pin_out = wiringPiGetPin(pilight_433_out);

	if(pilight_433_trans_initialized == 0) {
		if((pilight_433_fd_trans = open(pilight_433_socket, O_WRONLY)) < 0) {
			logprintf(LOG_ERR, "could not open %s", pilight_433_socket);
			return EXIT_FAILURE;
		} else {

			ioctl(pilight_433_fd_trans, IOCTL_GPIO_OUT, pin_out);

			if(ioctl(pilight_433_fd_trans, IOCTL_START_TRANSMITTER, 0) < 0){
				logprintf(LOG_ERR, "could not start pilight transmitter on pin %d", pin_out);
				return EXIT_FAILURE;
			}

			logprintf(LOG_DEBUG, "initialized pilight transmitter module");
			pilight_433_trans_initialized = 1;
		}
	}

	if(pilight_433_rec_initialized == 0) {
		if((pilight_433_fd_rec = open(pilight_433_socket, O_RDONLY)) < 0) {
			logprintf(LOG_ERR, "could not open %s", pilight_433_socket);
			return EXIT_FAILURE;
		} else {

			ioctl(pilight_433_fd_rec, IOCTL_GPIO_IN, pin_in);
			ioctl(pilight_433_fd_rec, IOCTL_LONGEST_V_P, pilight_433_lvp);
			ioctl(pilight_433_fd_rec, IOCTL_SHORTEST_V_P, pilight_433_svp);
			ioctl(pilight_433_fd_rec, IOCTL_FILTER_ON, filter_on);
			if(ioctl(pilight_433_fd_rec, IOCTL_START_RECEIVER, 0) < 0){
				logprintf(LOG_ERR, "could not start pilight receiver on pin %d", pin_in);
				return EXIT_FAILURE;
			}

			logprintf(LOG_DEBUG, "initialized pilight receiver module");
			pilight_433_rec_initialized = 1;
		}
	}

	return EXIT_SUCCESS;
}

unsigned short pilight433HwDeinit(void) {
	if(pilight_433_rec_initialized == 1) {
		ioctl(pilight_433_fd_rec, IOCTL_STOP_RECEIVER, 0);
		if(pilight_433_fd_rec != 0) {
			close(pilight_433_fd_rec);
			pilight_433_fd_rec = 0;
		}
		logprintf(LOG_DEBUG, "deinitialized pilight receiver module");
		pilight_433_rec_initialized	= 0;
	}

	if(pilight_433_trans_initialized == 1) {
		ioctl(pilight_433_fd_trans, IOCTL_STOP_TRANSMITTER, 0);
		if(pilight_433_fd_trans != 0) {
			close(pilight_433_fd_trans);
			pilight_433_fd_trans = 0;
		}

		logprintf(LOG_DEBUG, "deinitialized pilight transmitter module");
		pilight_433_trans_initialized	= 0;
	}

	return EXIT_SUCCESS;
}

int pilight433Send(int *code) {
	ssize_t ret = 0;
	unsigned long code_len = 0;

	while(code[code_len]) {
		code_len++;
	}
	code_len++;
	code_len*=sizeof(int);

	ret = write(pilight_433_fd_trans, code, code_len);

	if(ret == code_len){
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

int pilight433Receive(void) {
	char buff[255] = {0};
	if((read(pilight_433_fd_rec, buff, sizeof(buff))) < 0) {
		usleep(5000*1000);
		return 0;
	}
	return (atoi(buff));
}

unsigned short pilight433Settings(JsonNode *json) {
	if(strcmp(json->key, "socket") == 0) {
		if(json->tag == JSON_STRING) {
			pilight_433_socket = malloc(strlen(json->string_)+1);
			if(!pilight_433_socket) {
				logprintf(LOG_ERR, "out of memory");
				exit(EXIT_FAILURE);
			}
			strcpy(pilight_433_socket, json->string_);
		} else {
			return EXIT_FAILURE;
		}
	}
	if(strcmp(json->key, "sender") == 0) {
		if(json->tag == JSON_NUMBER) {
		pilight_433_in = (int)json->number_;
		} else {
			return EXIT_FAILURE;
		}
	}
	if(strcmp(json->key, "receiver") == 0) {
		if(json->tag == JSON_NUMBER) {
		pilight_433_out = (int)json->number_;
		} else {
			return EXIT_FAILURE;
		}
	}
	if(strcmp(json->key, "uc-connected") == 0) {
		if(json->tag == JSON_NUMBER) {
		pilight_433_prefilter = (int)json->number_;
		} else {
			return EXIT_FAILURE;
		}
	}
	if(strcmp(json->key, "shortest-pulse") == 0) {
		if(json->tag == JSON_NUMBER) {
		pilight_433_svp = (int)json->number_;
		} else {
			return EXIT_FAILURE;
		}
	}
	if(strcmp(json->key, "longest-pulse") == 0) {
		if(json->tag == JSON_NUMBER) {
		pilight_433_lvp = (int)json->number_;
		} else {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

int pilight433gc(void) {
	if(pilight_433_socket) {
		sfree((void *)&pilight_433_socket);
	}

	return EXIT_SUCCESS;
}


void pilight433Init(void) {

	gc_attach(pilight433gc);

	hardware_register(&pilight433);
	hardware_set_id(pilight433, "433pilight");

	options_add(&pilight433->options, 'd', "socket", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_STRING, NULL, "^/dev/([a-z]+)[0-9]+$");
	options_add(&pilight433->options, 'r', "receiver", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]+$");
	options_add(&pilight433->options, 's', "sender", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]+$");
	options_add(&pilight433->options, 'u', "uc-connected", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]+$");
	options_add(&pilight433->options, 'p', "shortest-pulse", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]+$");
	options_add(&pilight433->options, 'l', "longest-pulse", OPTION_HAS_VALUE, CONFIG_VALUE, JSON_NUMBER, NULL, "^[0-9]+$");

	pilight433->type=RF433;
	pilight433->init=&pilight433HwInit;
	pilight433->deinit=&pilight433HwDeinit;
	pilight433->send=&pilight433Send;
	pilight433->receive=&pilight433Receive;
	pilight433->settings=&pilight433Settings;
}

void compatibility(const char **version, const char **commit) {
	*version = "4.0";
	*commit = "18";
}

void init(void) {
	pilight433Init();
}
