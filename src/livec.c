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
#include <atomickit/atomic-rcp.h>
#include <atomickit/atomic-string.h>

#include "livec.h"
#include "local.h"

void watch_file() {
    int r;
    int notify_fd;
    int dwatch;
    struct astr *sfilename;

    notify_fd = inotify_init();
    if(notify_fd < 0) {
	perror(ERRORTEXT("Failed to initialize inotify system"));
	exit(EXIT_FAILURE);
    }

    for(;;) {
	sfilename = (struct astr *) arcp_load(&livec_opts.filename);
	if(sfilename == NULL) {
	    fprintf(stderr, ERRORTEXT("No filename defined\n"));
	    exit(EXIT_FAILURE);
	}
	{
	    char filebuf[astr_len(sfilename)];
	    char dirbuf[astr_len(sfilename)];
	    char *dir;
	    char *file;
	    bool modified;
	    strcpy(filebuf, astr_cstr(sfilename));
	    strcpy(dirbuf, astr_cstr(sfilename));
	    dir = dirname(dirbuf);
	    file = basename(filebuf);

	    dwatch = inotify_add_watch(notify_fd, dir, IN_CLOSE_WRITE|IN_MOVED_TO|IN_MOVE_SELF);
	    if(dwatch <= 0) {
		fprintf(stderr, ERRORTEXT("Failed to add inotify watch for %s") ": %s\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	    }
	    goto modified;
	    while(sfilename == (struct astr *) arcp_load_phantom(&livec_opts.filename)) {
		static uint8_t buf[sizeof(struct inotify_event) + NAME_MAX + 1];
		size_t i;
		ssize_t len;
		len = read(notify_fd, buf, sizeof(struct inotify_event) + NAME_MAX + 1);
		if(len <= 0) {
		    perror(ERRORTEXT("read() of inotify event failed"));
		    continue;
		}
		for(i = 0; i <= len - sizeof(struct inotify_event);) {
		    struct inotify_event *event = (struct inotify_event *) &buf[i];
		    i += sizeof(struct inotify_event) + event->len;

		    if(event->wd == dwatch) {
			if(event->mask & IN_MOVE_SELF) {
			    struct astr *newfilename;
			    newfilename = astr_alloc(strlen(event->name) + 1 + strlen(file) + 1);
			    if(newfilename == NULL) {
				perror(ERRORTEXT("Failed to create new file string"));
				exit(EXIT_FAILURE);
			    }
			    astr_cstrcpy(newfilename, event->name);
			    astr_cstrcat(newfilename, "/");
			    astr_cstrcat(newfilename, file);
			    arcp_compare_store(&livec_opts.filename, sfilename, newfilename);
			    arcp_release(sfilename);
			    sfilename = newfilename;
			} else if((event->mask & (IN_CLOSE_WRITE|IN_MOVED_TO))
				  && (strcmp(event->name, file) == 0)) {
			    modified = true;
			}
		    }
		}
		if(modified) {
		modified:
		    {
			char *dsofile;
			struct dso_entry *entry;
			modified = false;
			r = fprintf(stderr, PROCTEXT("Compiling %s...\n"), astr_cstr(sfilename));
			if(r < 0) {
			    perror(ERRORTEXT("Unable to print compilation notification"));
			}
			dsofile = compile(sfilename);
			if(dsofile == NULL) {
			    r = fprintf(stderr, ERRORTEXT("Compilation failed.\n"));
			    if(r < 0) {
				perror(ERRORTEXT("Unable to print compilation failure message"));
			    }
			    continue;
			}
			r = fprintf(stderr, SUCCESSTEXT("Compilation succeeded.\n"));
			if(r < 0) {
			    perror(ERRORTEXT("Unable to print compilation success message"));
			}
			r = fprintf(stderr, PROCTEXT("Loading %s...\n"), dsofile);
			if(r < 0) {
			    perror(ERRORTEXT("Unable to print load notification"));
			}
			entry = load(dsofile);
			if(entry == NULL) {
			    r = fprintf(stderr, ERRORTEXT("Load failed.\n"));
			    if(r < 0) {
				perror(ERRORTEXT("Unable to print load failure message"));
			    }
			    r = unlink(dsofile);
			    if(r != 0) {
				fprintf(stderr, ERRORTEXT("Failed to unlink %s") ": %s\n", dsofile, strerror(errno));
			    }
			    continue;
			}
			r = fprintf(stderr, SUCCESSTEXT("Load succeeded.\n"));
			if(r < 0) {
			    perror(ERRORTEXT("Unable to print load success message"));
			}
			run(entry);
		    }
		}
	    }
	}
	arcp_release(sfilename);
	r = inotify_rm_watch(notify_fd, dwatch);
	if(r != 0) {
	    perror(ERRORTEXT("Failed to clean up old watch"));
	}
    }
}
