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

