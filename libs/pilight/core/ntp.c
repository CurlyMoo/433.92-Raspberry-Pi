/*
	Copyright (C) 2014 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define MSG_NOSIGNAL 0
#else
	#ifdef __mips__
		#define __USE_UNIX98
	#endif
	#include <sys/socket.h>
	#include <sys/time.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <netdb.h>
	#include <arpa/inet.h>
#endif
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#include "network.h"
#include "socket.h"
#include "pilight.h"
#include "threadpool.h"
#include "eventpool.h"
#include "common.h"
#include "log.h"
#include "../storage/storage.h"

typedef struct l_fp {
	union {
		unsigned int Xl_ui;
		int Xl_i;
	} Ul_i;
	union {
		unsigned int Xl_uf;
		int Xl_f;
	} Ul_f;
} l_fp;

typedef struct pkt {
	int	li_vn_mode;
	int rootdelay;
	int rootdispersion;
	int refid;
	struct l_fp ref;
	struct l_fp org;
	struct l_fp rec;
	/* Make sure the pkg is 48 bits */
	double tmp;
} pkt;

static volatile int running = 0;
static volatile int processing = 0;
static int diff = 0;
static int synced = -1;
static int ntptime = -1;
static int cursor = 0;
static char **ntpservers = NULL;
static unsigned int nrservers = 0;

void *ntpthread(void *param);

int ntp_gc(void) {
	int x = 0;
	logprintf(LOG_DEBUG, "garbage collected ntp library");

	if(ntpservers != NULL) {
		for(x=0;x<nrservers;x++) {
			FREE(ntpservers[x]);
		}
		FREE(ntpservers);
	}

	return 0;
}

static int process_cursor(void) {
	struct timeval tv;
	cursor++;

	running = 0;

	if(ntptime == -1) {
		if(cursor >= nrservers) {
			cursor = 0;
			tv.tv_sec = 10;
			tv.tv_usec = 0;
			threadpool_add_scheduled_work("ntp sync", ntpthread, tv, NULL);
		}	else {
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			threadpool_add_scheduled_work("ntp sync", ntpthread, tv, NULL);
		}
	} else {
		tv.tv_sec = 86400;
		tv.tv_usec = 0;
		threadpool_add_scheduled_work("ntp sync", ntpthread, tv, NULL);
	}
	return 0;
}

static int callback(struct eventpool_fd_t *node, int event) {
	struct pkt msg;
	int x = 0;

	memset(&msg, '\0', sizeof(struct pkt));

	switch(event) {
		case EV_CONNECT_FAILED:
			eventpool_fd_remove(node);
			process_cursor();
		break;
		case EV_CONNECT_SUCCESS:
			eventpool_fd_enable_write(node);
		break;
		case EV_WRITE:
			msg.li_vn_mode = 227;
			if((x = send(node->fd, (char *)&msg, 48, 0)) != 48) {
				return -1;
			}
			eventpool_fd_enable_read(node);
			return 0;
		break;
		case EV_READ: {
			int x = 0;
			if((x = recv(node->fd, (void *)&msg, 48, 0)) == 48) {
				if(msg.refid > 0) {
					(msg.rec).Ul_i.Xl_ui = ntohl((msg.rec).Ul_i.Xl_ui);
					(msg.rec).Ul_f.Xl_f = (int)ntohl((unsigned int)(msg.rec).Ul_f.Xl_f);

					processing = 1;

					unsigned int adj = 2208988800u;
					ntptime = (time_t)(msg.rec.Ul_i.Xl_ui - adj);

					diff = (int)(time(NULL) - ntptime);
					logprintf(LOG_INFO, "time offset found of %d seconds", diff);
					synced = 1;
					processing = 0;
				} else {
					logprintf(LOG_INFO, "could not sync with ntp server: %s", node->data.socket.server);
				}
			}
			eventpool_fd_remove(node);
			process_cursor();
			return 0;
		}
		break;
		case EV_DISCONNECTED:
			eventpool_fd_remove(node);
			process_cursor();
		break;
		default:
		break;
	}
	return 0;
}

void *ntpthread(void *param) {
	char *tmp = NULL;

	if(running == 1) {
		struct timeval tv;

		tv.tv_sec = 60;
		tv.tv_usec = 0;
		threadpool_add_scheduled_work("ntp sync", ntpthread, tv, NULL);

		logprintf(LOG_DEBUG, "%s already running", ((struct threadpool_tasks_t *)param)->name);
		return NULL;
	}

	running = 1;

	if(ntpservers == NULL) {
		while(settings_select_string_element(ORIGIN_MASTER, "ntp-servers", nrservers, &tmp) == 0) {
			if((ntpservers = REALLOC(ntpservers, sizeof(char *)*(nrservers+1))) == NULL) {
				OUT_OF_MEMORY
			}
			if((ntpservers[nrservers] = MALLOC(strlen(tmp)+1)) == NULL) {
				OUT_OF_MEMORY
			}
			strcpy(ntpservers[nrservers], tmp);
			nrservers++;
		}

		if(nrservers == 0) {
			FREE(ntpservers);
			logprintf(LOG_NOTICE, "no ntp-servers defined in the config settings");
			running = 0;
			return 0;
		}
	}

	logprintf(LOG_DEBUG, "trying to sync with ntp-server %s", ntpservers[cursor]);

	eventpool_socket_add("ping", ntpservers[cursor], 123, AF_INET, SOCK_DGRAM, 0, EVENTPOOL_TYPE_SOCKET_CLIENT, callback, NULL, NULL);

	return (void *)NULL;
}

int getntpdiff(void) {
	if(processing == 1) {
		return 0;
	}
	return diff;
}

int isntpsynced(void) {
	if(processing == 1) {
		return -1;
	}
	return synced;
}
