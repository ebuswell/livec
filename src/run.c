/* run.c running the compiled file.
 *
 * Copyright 2013 Evan Buswell
 *
 * This file is part of Live C.
 *
 * Live C is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * Live C is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Live C.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <atomickit/rcp.h>

#include "livec.h"
#include "local.h"

/* the thread-specific storage key for the dso_entry */
pthread_key_t entry_key;

/* generic state structure */
arcp_t state = ARCP_VAR_INIT(NULL);

/* the args from the commandline to be passed into the function */
struct argstruct args __attribute__((visibility("hidden"))) = { 0, NULL };

/* this function is the content of the thread */
static void *thread_run(struct dso_entry *entry) {
	int r;
	pthread_setspecific(entry_key, entry);
	r = entry->proc(args.argc, args.argv);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Thread exited with error code %d\n"), r);
	} else {
		fprintf(stderr, SUCCESSTEXT("Thread finished\n"));
	}
	return NULL;
}

/* run the given dso_entry in a separate thread */
void run(struct dso_entry *entry) {
	int r;
	pthread_attr_t attr;
	pthread_t thread;

	r = pthread_attr_init(&attr);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Fatal: Failed to initialize new"
		                          " thread attributes") ": %s\n",
		        strerror(r));
		arcp_release(entry);
		exit(EXIT_FAILURE);
	}
	r = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Fatal: Failed to set detached"
		                          " thread attribute") ": %s\n",
		        strerror(r));
		arcp_release(entry);
		exit(EXIT_FAILURE);
	}

	r = pthread_create(&thread, &attr,
	                   (void *(*)(void *)) thread_run, entry);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Fatal: Failed to create new"
		                          " thread") ": %s\n",
		        strerror(r));
		arcp_release(entry);
		exit(EXIT_FAILURE);
	}

	r = pthread_attr_destroy(&attr);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Failed to clean up new thread"
		                          " attributes") ": %s\n",
		        strerror(r));
	}
}

/* sync signal handler */
static void handle_fatal_signal(int signum, siginfo_t *info,
                                void *context __attribute__((unused))) {
	if(pthread_self() == main_thread) {
		/* do default action (die in some way) */
		signal(signum, SIG_DFL);
		raise(signum);
	} else {
		/* terminate the receiving thread */
		psiginfo(info, ERRORTEXT("Thread received fatal signal"));
		pthread_exit(NULL);
	}
}

void setup_signal_handling() {
	int r;
	struct sigaction act;

	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = handle_fatal_signal;

	r = sigemptyset(&act.sa_mask);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to clear signal mask"));
		exit(EXIT_FAILURE);
	}
	r = sigaddset(&act.sa_mask, SIGABRT);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to add SIGABRT to signal"
		                 " mask"));
		exit(EXIT_FAILURE);
	}
	r = sigaddset(&act.sa_mask, SIGBUS);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to add SIGBUS to signal"
		                 " mask"));
		exit(EXIT_FAILURE);
	}
	r = sigaddset(&act.sa_mask, SIGFPE);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to add SIGBUS to signal"
		                 " mask"));
		exit(EXIT_FAILURE);
	}
	r = sigaddset(&act.sa_mask, SIGILL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to add SIGILL to signal"
		                 " mask"));
		exit(EXIT_FAILURE);
	}
	r = sigaddset(&act.sa_mask, SIGSEGV);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to add SIGSEGV to signal"
		                 " mask"));
		exit(EXIT_FAILURE);
	}
	r = sigaddset(&act.sa_mask, SIGSYS);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to add SIGSYS to signal"
		                 " mask"));
		exit(EXIT_FAILURE);
	}

	r = sigaction(SIGABRT, &act, NULL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to install handler for signal"
		                 " SIGABRT"));
		exit(EXIT_FAILURE);
	}
	r = sigaction(SIGBUS, &act, NULL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to install handler for signal"
		                 " SIGBUS"));
		exit(EXIT_FAILURE);
	}
	r = sigaction(SIGFPE, &act, NULL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to install handler for signal"
		                 " SIGFPE"));
		exit(EXIT_FAILURE);
	}
	r = sigaction(SIGILL, &act, NULL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to install handler for signal"
		                 " SIGILL"));
		exit(EXIT_FAILURE);
	}
	r = sigaction(SIGSEGV, &act, NULL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to install handler for signal"
		                 " SIGSEGV"));
		exit(EXIT_FAILURE);
	}
	r = sigaction(SIGSYS, &act, NULL);
	if(r != 0) {
		perror(ERRORTEXT("Fatal: Failed to install handler for signal"
		                 " SIGSYS"));
		exit(EXIT_FAILURE);
	}
}
