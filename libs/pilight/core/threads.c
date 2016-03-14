/*
	Copyright (C) 2013 - 2016 CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#ifndef _WIN32
	#ifdef __mips__
		#define __USE_UNIX98
	#endif
#endif
#include <pthread.h>
#include <sys/time.h>

#include "threads.h"
#include "common.h"
#include "log.h"
#include "mem.h"

static unsigned short thread_loop = 1;
static unsigned short thread_running = 0;
static unsigned int threads_initialized = 0;

static pthread_mutex_t threadqueue_lock;
static pthread_cond_t threadqueue_signal;
static pthread_mutexattr_t threadqueue_attr;

static int threadqueue_number = 0;
static struct threadqueue_t *threadqueue;
static int threads_loop_running = 0;
static int thread_started = 0;
static pthread_t pth;

struct threadqueue_t *threads_register(const char *id, void *(*function)(void *param), void *param, int force) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	if(threads_initialized == 1) {
		pthread_mutex_lock(&threadqueue_lock);
	}

	struct threadqueue_t *tnode = MALLOC(sizeof(struct threadqueue_t));
	if(tnode == NULL) {
		OUT_OF_MEMORY
	}

	struct timeval tcurrent;
	memset(&tcurrent, '\0', sizeof(struct timeval));
	gettimeofday(&tcurrent, NULL);

	tnode->function = function;
	tnode->running = 0;
	tnode->join = 0;
	tnode->force = force;
	if(id != NULL) {
		if((tnode->id = MALLOC(strlen(id)+1)) == NULL) {
			OUT_OF_MEMORY
		}
		strcpy(tnode->id, id);
	}
	tnode->param = param;
	memset(&tnode->pth, '\0', sizeof(pthread_t));
	tnode->next = NULL;

	memset(&tnode->cpu_usage, '\0', sizeof(struct cpu_usage_t));
	tnode->cpu_usage.cpu_old = 0;
	tnode->cpu_usage.cpu_per = 0;
	tnode->cpu_usage.cpu_new = 0;
	tnode->cpu_usage.sec_start = 0;
	tnode->cpu_usage.sec_stop = 0;
	tnode->cpu_usage.sec_diff = 0;
	memset(&tnode->cpu_usage.ts, '\0', sizeof(struct timespec));
	tnode->cpu_usage.starts = 0;

	struct threadqueue_t *tmp = threadqueue;
	if(tmp) {
		while(tmp->next != NULL) {
			tmp = tmp->next;
		}
		tmp->next = tnode;
	} else {
		tnode->next = tmp;
		threadqueue = tnode;
	}
	threadqueue_number++;

	if(threads_initialized == 1) {
		pthread_mutex_unlock(&threadqueue_lock);
		pthread_cond_signal(&threadqueue_signal);
	}

	return tnode;
}

void threads_create(pthread_t *thread, const pthread_attr_t *attr,  void *(*start_routine)(void *), void *arg) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

#ifndef _WIN32
	sigset_t new, old;
	sigemptyset(&new);
	sigaddset(&new, SIGINT);
	sigaddset(&new, SIGQUIT);
	sigaddset(&new, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &new, &old);
#endif
	pthread_create(thread, attr, start_routine, arg);
#ifndef _WIN32
	pthread_sigmask(SIG_SETMASK, &old, NULL);
#endif
}

void thread_signal(char *id, int s) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	struct threadqueue_t *tmp_threads = threadqueue;
	while(tmp_threads) {
		if(strcmp(tmp_threads->id, id) == 0) {
			pthread_kill(tmp_threads->pth, s);
			break;
		}
		tmp_threads = tmp_threads->next;
	}
}

static void thread_remove(char *id) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);
	if(threads_initialized == 1) {
		pthread_mutex_lock(&threadqueue_lock);
	}

	struct threadqueue_t *currP, *prevP;

	prevP = NULL;

	for(currP = threadqueue; currP != NULL; prevP = currP, currP = currP->next) {

		if(strcmp(currP->id, id) == 0) {
			if(prevP == NULL) {
				threadqueue = currP->next;
			} else {
				prevP->next = currP->next;
			}

			thread_running--;
			FREE(currP->id);
			FREE(currP);

			break;
		}
	}
	if(threads_initialized == 1) {
		pthread_mutex_unlock(&threadqueue_lock);
	}
}

static void *threads_loop(void *param) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);
	int start = 0, join = 0;

	struct threadqueue_t *tmp_threads = NULL;

	pthread_mutex_lock(&threadqueue_lock);
	threads_loop_running = 1;
	while(thread_loop) {
		if(threadqueue_number > 0) {
			pthread_mutex_lock(&threadqueue_lock);

			logprintf(LOG_STACK, "%s::unlocked", __FUNCTION__);

			join = 0, start = 0;
			tmp_threads = threadqueue;
			while(tmp_threads) {
				if(tmp_threads->running == 0) {
					start = 1;
					break;
				} else if(tmp_threads->join == 1) {
					join = 1;
					break;
				}
				tmp_threads = tmp_threads->next;
			}
			if(start == 1) {
				threads_create(&tmp_threads->pth, NULL, tmp_threads->function, (void *)tmp_threads->param);
				thread_running++;
				tmp_threads->running = 1;
				if(thread_running == 1) {
					logprintf(LOG_DEBUG, "new thread \"%s\", %d thread running", tmp_threads->id, thread_running);
				} else {
					logprintf(LOG_DEBUG, "new thread \"%s\", %d threads running", tmp_threads->id, thread_running);
				}
			} else if(join == 1) {
				if(tmp_threads->force == 1) {
					pthread_cancel(tmp_threads->pth);
				} else {
					pthread_join(tmp_threads->pth, NULL);
				}
				if((thread_running-1) == 1) {
					logprintf(LOG_DEBUG, "stopped thread %s, %d thread running", tmp_threads->id, (thread_running-1));
				} else {
					logprintf(LOG_DEBUG, "stopped thread %s, %d threads running", tmp_threads->id, (thread_running-1));
				}
				thread_remove(tmp_threads->id);
			}

			threadqueue_number--;
			pthread_mutex_unlock(&threadqueue_lock);
		} else {
			pthread_cond_wait(&threadqueue_signal, &threadqueue_lock);
		}
	}
	threads_loop_running = 0;
	return (void *)NULL;
}

void threads_start() {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	if(threads_initialized == 0) {
		pthread_mutexattr_init(&threadqueue_attr);
		pthread_mutexattr_settype(&threadqueue_attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&threadqueue_lock, &threadqueue_attr);
		pthread_cond_init(&threadqueue_signal, NULL);
		threads_initialized = 1;
	}

	threads_create(&pth, NULL, &threads_loop, (void *)NULL);
	thread_started = 1;
}

int thread_exists(char *id) {
	if(threads_initialized == 1) {
		pthread_mutex_lock(&threadqueue_lock);
	}
	struct threadqueue_t *tmp_threads = threadqueue;
	while(tmp_threads) {
		if(strcmp(id, tmp_threads->id) == 0) {
			if(threads_initialized == 1) {
				pthread_mutex_unlock(&threadqueue_lock);
			}
			return 0;
		}
		tmp_threads = tmp_threads->next;
	}
	if(threads_initialized == 1) {
		pthread_mutex_unlock(&threadqueue_lock);
	}
	return -1;
}

void thread_stop(char *id) {
	if(threads_initialized == 1) {
		pthread_mutex_lock(&threadqueue_lock);
	}
	struct threadqueue_t *tmp_threads = threadqueue;
	while(tmp_threads) {
		if(strcmp(id, tmp_threads->id) == 0) {
			logprintf(LOG_DEBUG, "stopping thread \"%s\"", id);

			tmp_threads->join = 1;

			threadqueue_number++;
			break;
		}
		tmp_threads = tmp_threads->next;
	}
	if(threads_initialized == 1) {
		pthread_mutex_unlock(&threadqueue_lock);
		pthread_cond_signal(&threadqueue_signal);
	}
}

void threads_cpu_usage(int print) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	if(print == 1) {
		logprintf(LOG_INFO, "----- Thread Profiling -----");
	}
	struct threadqueue_t *tmp_threads = threadqueue;
	while(tmp_threads) {
		if(tmp_threads->running == 1) {
			getThreadCPUUsage(tmp_threads->pth, &tmp_threads->cpu_usage);
			if(print) {
				if(tmp_threads->cpu_usage.cpu_per > 0) {
					logprintf(LOG_INFO, "- thread %s: %f%%", tmp_threads->id, tmp_threads->cpu_usage.cpu_per);
				} else {
					logprintf(LOG_INFO, "- thread %s: 0.000000%%", tmp_threads->id);
				}
			}
		}
		tmp_threads = tmp_threads->next;
	}
	if(print == 1) {
		logprintf(LOG_INFO, "----- Thread Profiling -----");
	}
}

int threads_gc(void) {
	logprintf(LOG_STACK, "%s(...)", __FUNCTION__);

	thread_loop = 0;

	if(threads_initialized == 1) {
		pthread_mutex_unlock(&threadqueue_lock);
		pthread_cond_signal(&threadqueue_signal);
	}

	struct threadqueue_t *tmp_threads = threadqueue;
	while(tmp_threads) {
		if(tmp_threads->running == 1) {
			tmp_threads->running = 0;
			thread_running--;

			logprintf(LOG_DEBUG, "stopping \"%s\" thread", tmp_threads->id);
			if(tmp_threads->force == 1) {
				pthread_cancel(tmp_threads->pth);
			}
			pthread_join(tmp_threads->pth, NULL);
			if(thread_running == 1) {
				logprintf(LOG_DEBUG, "stopped thread \"%s\", %d thread running", tmp_threads->id, thread_running);
			} else {
				logprintf(LOG_DEBUG, "stopped thread \"%s\", %d threads running", tmp_threads->id, thread_running);
			}
		}
		usleep(10000);
		tmp_threads = tmp_threads->next;
	}

	struct threadqueue_t *ttmp = NULL;
	while(threadqueue) {
		ttmp = threadqueue;
		if(ttmp->id != NULL) {
			FREE(ttmp->id);
		}
		threadqueue = threadqueue->next;
		FREE(ttmp);
	}
	if(threadqueue != NULL) {
		FREE(threadqueue);
	}

	while(threads_loop_running > 0) {
		usleep(10);
	}
	if(thread_started == 1) {
		pthread_join(pth, NULL);
	}

	logprintf(LOG_DEBUG, "garbage collected threads library");
	return EXIT_SUCCESS;
}
