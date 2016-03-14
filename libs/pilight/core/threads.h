/*
	Copyright (C) 2013 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _THREADS_H_
#define _THREADS_H_

#ifdef _WIN32
	#include "windows.h"
#endif
#include <pthread.h>
#include "proc.h"

struct threadqueue_t {
	pthread_t pth;
#ifdef _WIN32
	HANDLE handle;
#endif
	int force;
	int join;
	char *id;
	void *param;
	int running;
	struct cpu_usage_t cpu_usage;
	void *(*function)(void *param);
	struct threadqueue_t *next;
} threadqueue_t;

struct threadqueue_t *threads_register(const char *id, void *(*function)(void* param), void *param, int force);
void threads_create(pthread_t *pth, const pthread_attr_t *attr,  void *(*start_routine) (void *), void *arg);
void threads_start(void);
void thread_stop(char *id);
int thread_exists(char *id);
void threads_cpu_usage(int print);
int threads_gc(void);
void thread_signal(char *id, int signal);

#endif
