/* livec.c Minimalist livecoding for C
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
#include <unistd.h>
#include <sys/inotify.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <atomickit/rcp.h>
#include <atomickit/string.h>

#include "livec.h"
#include "local.h"

/* compile, link, and load the file */
static void process_file(struct astr *sfilename) {
	char *dsofile;
	struct dso_entry *entry;
	int r;
	fprintf(stderr, PROCTEXT("Compiling %s...\n"), astr_cstr(sfilename));
	dsofile = compile(sfilename);
	if(dsofile == NULL) {
		fprintf(stderr, ERRORTEXT("Compilation failed.\n"));
		return;
	}
	fprintf(stderr, SUCCESSTEXT("Compilation succeeded.\n"));
	fprintf(stderr, PROCTEXT("Loading %s...\n"), dsofile);
	entry = load(dsofile);
	if(entry == NULL) {
		fprintf(stderr, ERRORTEXT("Load failed.\n"));
		r = unlink(dsofile);
		if(r != 0) {
			fprintf(stderr, ERRORTEXT("Failed to unlink %s")
			        ": %s\n", dsofile, strerror(errno));
		}
		return;
	}
	fprintf(stderr, SUCCESSTEXT("Load succeeded.\n"));
	run(entry);
}

/* set up the appropriate watch on the directory indicated by sfilename */
static int setup_inotify_watch(int notify_fd, struct astr *sfilename) {
	char dirbuf[astr_len(sfilename) + 1];
	char *dir;
	int dwatch;

	strcpy(dirbuf, astr_cstr(sfilename));
	dir = dirname(dirbuf);
	
	dwatch = inotify_add_watch(notify_fd, dir,
	                           IN_CLOSE_WRITE|IN_MOVED_TO);
	if(dwatch <= 0) {
		fprintf(stderr,
		        ERRORTEXT("Fatal: failed to add inotify watch for %s")
		        ": %s\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return dwatch;
}

/* basename which is guaranteed not to modify filename */
static char *simple_basename(char *filename) {
	char *ret = strrchr(filename, '/');
	return ret == NULL ? filename : ++ret;
}

void watch_file() {
	int r;
	int notify_fd;
	int dwatch;
	struct astr *sfilename;
	char *file;
	uint8_t inotify_buf[sizeof(struct inotify_event) + NAME_MAX + 1];
	struct inotify_event *event;
	size_t i;
	ssize_t len;

	/* initialize the inotify system */
	notify_fd = inotify_init();
	if(notify_fd < 0) {
		perror(ERRORTEXT("Fatal: failed to initialize "
		                 "inotify system"));
		exit(EXIT_FAILURE);
	}

setup_watch:
	/* set up the inotify watch and associated variables */
	sfilename = (struct astr *) arcp_load(&livec_opts.filename);
	if(sfilename == NULL) {
		fprintf(stderr, ERRORTEXT("Fatal: no filename defined\n"));
		exit(EXIT_FAILURE);
	}

	file = simple_basename(astr_cstr(sfilename));

	dwatch = setup_inotify_watch(notify_fd, sfilename);

	/* process the file */
	process_file(sfilename);

	/* main watch loop */
	for(;;) {
		if(sfilename != (struct astr *)
		   arcp_load_phantom(&livec_opts.filename)) {
			/* the filename option has changed; remove the watch
 			 * and restart */
			arcp_release(sfilename);
			r = inotify_rm_watch(notify_fd, dwatch);
			if(r != 0) {
				perror(ERRORTEXT("Failed to clean"
				                 " up old watch"));
			}
			goto setup_watch;
		}
		/* block until there's at least one event to be notified
 		 * about */
		len = read(notify_fd, inotify_buf,
		           sizeof(struct inotify_event) + NAME_MAX + 1);
		if(len <= 0) {
			perror(ERRORTEXT("read() of inotify event failed"));
			continue;
		}
		/* read through all events */
		for(i = 0; i <= len - sizeof(struct inotify_event);) {
			event = (struct inotify_event *) &inotify_buf[i];
			i += sizeof(struct inotify_event) + event->len;

			if(event->wd != dwatch) {
				/* FIXME: why wouldn't this be the same? */
				continue;
			}
			if((event->mask & (IN_CLOSE_WRITE|IN_MOVED_TO))
			   && (strcmp(event->name, file) == 0)) {
				/* FIXME: do we need the event mask test
 				 * here? */
				/* the (directory) event was about the file
 				 * we're interested in */
				process_file(sfilename);
				break;
			}
		}
	}
}
