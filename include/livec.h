/* livec.h Minimalist livecoding for C
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

#ifndef LIVEC_H
#define LIVEC_H 1

#include <pthread.h>
#include <atomickit/atomic-rcp.h>
#include <atomickit/atomic-string.h>
#include <witch.h>

/**
 * The state structure. One of these is passed in to every call.
 */
struct livec_opts {
    arcp_t filename; /** The filename of the file we're compiling and
		      * running. */
    arcp_t compiler; /** The compiler to use. */
    arcp_t ldflags; /** Collected LDFLAGS. */
    arcp_t cflags; /** Collected CFLAGS. */
    arcp_t builddir; /** The directory in which we build the dso
		      * files. */
    arcp_t entry; /** Name of entry point. */
};

struct dso_entry;

typedef int (*livec_proc)(int argc, char **argv);

struct dso_entry {
    struct arcp_region;
    livec_proc proc;
    void *dlhandle;
    char *dsofile;
};

extern pthread_key_t entry_key;

extern struct livec_opts livec_opts;

struct dso_afptr {
    struct afptr;
    struct dso_entry *entry;
};

struct alink_entry {
    struct arcp_region;
    arcp_t afptr;
    struct astr *fname;
    void *dispatch_fptr;
};

extern arcp_t state; /** Generic pointer to some sort of state
		      * structure. */

void *autolink_create(void *fptr, char *fname, char *signature);

#define AUTOLINK_CREATE(fptr, signature)	\
    autolink_create(fptr, #fptr, signature)

int autolink_destroy(char *fname);

#endif /* ! LIVEC_H*/
