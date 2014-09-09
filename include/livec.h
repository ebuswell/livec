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
#include <atomickit/rcp.h>
#include <atomickit/string.h>
#include <witch.h>

/**
 * The state structure.
 */
struct livec_opts {
	arcp_t filename; /**< The filename of the file we're compiling and
	                  *   running. */
	arcp_t compiler; /**< The compiler to use. */
	arcp_t ldflags; /**< Collected LDFLAGS. */
	arcp_t cflags; /**< Collected CFLAGS. */
	arcp_t builddir; /**< The directory in which we build the dso
	                  *   files. */
	arcp_t entry; /**< Name of entry point. */
};

/**
 * The global options structure.
 */
extern struct livec_opts livec_opts;

/**
 * The type of function the Live C process expects; patterned on C's main.
 */
typedef int (*livec_proc)(int argc, char **argv);

/**
 * Struct representing the loaded DSO file. The file will be unloaded when the
 * reference count for this structure reaches zero.
 */
struct dso_entry {
	struct arcp_region;
	livec_proc proc; /**< The entry function */
	void *dlhandle; /**< The handle for the dso file */
	char *dsofile; /**< The filename of the dso file */
};

/**
 * Thread-specific storage of the entry key. Since each iteration of the load
 * process will take place in a different thread, this is how the livec entry
 * key (the struct dso_entry) is passed to the compiled application.
 */
extern pthread_key_t entry_key;

/**
 * Generic pointer to some sort of state structure.
 */
extern arcp_t state; 

/**
 * Create an autolink function. An autolink function is automatically relinked
 * on recompile to a new function of the corresponding name.
 *
 * @param fptr the function pointer for the initial link.
 * @param fname the function name.
 * @param signature the signature for the function pointer.
 * @returns a function pointer that dispatches to a function of the given name.
 */
void *autolink_create(void *fptr, char *fname, char *signature);

/**
 * Create an autolink function, convenience version which sets the function
 * name to be the same as the name of the passed in function pointer.
 */
#define AUTOLINK_CREATE(fptr, signature)	\
	autolink_create(fptr, #fptr, signature)

/**
 * Destroy an autolink function.
 */
int autolink_destroy(char *fname);

#endif /* ! LIVEC_H*/
