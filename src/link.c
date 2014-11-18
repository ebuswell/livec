/* link.c Linking and loading
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
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <atomickit/rcp.h>
#include <atomickit/malloc.h>
#include <atomickit/dict.h>
#include <atomickit/string.h>
#include <witch.h>

#include "livec.h"
#include "local.h"

/**
 * An atomic function pointer that takes into account the DSO in which the
 * function exists. This allows for unloading of the DSO when there are no
 * longer any function pointers referencing it.
 */
struct dso_afptr {
	struct afptr;
	struct dso_entry *entry; /**< The DSO entry in which the pointed to
	                              function is found. */
};

/**
 * Autolink entry. An autolink function is automatically relinked on recompile
 * to a new function of the corresponding name.
 */
struct alink_entry {
	struct arcp_region;
	arcp_t afptr; /**< The arcp_t in which the function to be dispatched
	                   to is stored. */
	void *dispatch_fptr;
};

static arcp_t autolink_table = ARCP_VAR_INIT(NULL);

static void dso_afptr_destroy(struct dso_afptr *afptr) {
	arcp_release(afptr->entry);
	afree(afptr, sizeof(struct dso_afptr));
}

static struct dso_afptr *dso_afptr_create(struct dso_entry *entry, void *fptr) {
	struct dso_afptr *afptr;
	afptr = amalloc(sizeof(struct dso_afptr));
	if(afptr == NULL) {
		return NULL;
	}

	afptr_init(afptr, fptr, (void (*)(struct afptr *)) dso_afptr_destroy);
	afptr->entry = (struct dso_entry *) arcp_acquire(entry);
	return afptr;
}

static void alink_entry_destroy(struct alink_entry *entry) {
	arcp_store(&entry->afptr, NULL);
	afptr_dispatch_free(entry->dispatch_fptr);
	afree(entry, sizeof(struct alink_entry));
}

static struct alink_entry *alink_entry_create(struct dso_afptr *afptr,
                                              char *signature) {
	struct alink_entry *entry;
	entry = amalloc(sizeof(struct alink_entry));
	if(entry == NULL) {
		return NULL;
	}
	arcp_region_init(entry, (void (*)(struct arcp_region *)) alink_entry_destroy);
	arcp_init(&entry->afptr, NULL);
	entry->dispatch_fptr = afptr_dispatch_create(&entry->afptr, signature);
	if(entry->dispatch_fptr == NULL) {
		afree(entry, sizeof(struct alink_entry));
		return NULL;
	}
	arcp_store(&entry->afptr, afptr);

	return entry;
}

void *autolink_create(void *fptr, char *fname, char *signature) {
	struct alink_entry *entry;
	struct dso_afptr *afptr;
	struct dso_entry *dso_entry;
	struct adict *entry_table;
	struct adict *new_entry_table;
	void *ret;

	dso_entry = (struct dso_entry *) pthread_getspecific(entry_key);
	if(dso_entry == NULL) {
		errno = EINVAL;
		return NULL;
	}

	afptr = dso_afptr_create(dso_entry, fptr);
	if(afptr == NULL) {
		return NULL;
	}

	entry = alink_entry_create(afptr, signature);
	arcp_release(afptr);
	if(entry == NULL) {
		return NULL;
	}

	do {
		entry_table = (struct adict *) arcp_load(&autolink_table);
		if(entry_table == NULL) {
			new_entry_table = adict_create_cstrput(fname, entry);
		} else {
			new_entry_table = adict_dup_cstrput(
					entry_table, fname, entry);
		}
		if(new_entry_table == NULL) {
			arcp_release(entry);
			return NULL;
		}
	} while(!arcp_cas_release(&autolink_table,
	                          entry_table, new_entry_table));

	ret = entry->dispatch_fptr;
	arcp_release(entry);
	return ret;
}

int autolink_destroy(char *fname) {
	struct adict *entry_table;
	struct adict *new_entry_table;

	/* Remove entry from entry table */
	do {
		entry_table = (struct adict *) arcp_load(&autolink_table);
		if(entry_table == NULL) {
			errno = EINVAL;
			return -1;
		}
		new_entry_table = adict_dup_cstrdel(entry_table, fname);
		if(new_entry_table == NULL) {
			arcp_release(entry_table);
			return -1;
		}
	} while(!arcp_cas_release(&autolink_table, entry_table, new_entry_table));

	return 0;
}

/* relink all the autolink functions to the versions in the given dso */
static int autolink_relink(struct dso_entry *dso_entry) {
	int ret = 0;
	int i, len;
	struct alink_entry *entry;
	struct astr *fname;
	struct adict *entry_table;
	void *fptr;
	struct dso_afptr *afptr;

	entry_table = (struct adict *) arcp_load(&autolink_table);
	if(entry_table == NULL) {
		return 0;
	}

	len = adict_len(entry_table);

	for(i = 0; i < len; i++) {
		fname = entry_table->items[i].key;
		entry = (struct alink_entry *) entry_table->items[i].value;
		fptr = dlsym(dso_entry->dlhandle, astr_cstr(fname));
		if(fptr == NULL) {
			arcp_store(&entry->afptr, NULL);
			fprintf(stderr,
			        ERRORTEXT("Could not find '%s'"
				          " function in %s\n"),
			        astr_cstr(fname), dso_entry->dsofile);
			continue;
		}
		afptr = dso_afptr_create(dso_entry, fptr);
		if(afptr == NULL) {
			fprintf(stderr,
			        ERRORTEXT("Could not create afptr"
				          " for function '%s'\n"),
			        astr_cstr(fname));
			ret = -1;
			continue;
		}
		arcp_store(&entry->afptr, afptr);
	}
	return ret;
}

/* destruction function for dso_entry; should unlink the file and dlclose the
 * handle */
static void dso_entry_destroy(struct dso_entry *entry) {
	int r;
	r = unlink(entry->dsofile);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Failed to unlink %s") ": %s\n",
		        entry->dsofile, strerror(errno));
	}
	r = dlclose(entry->dlhandle);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Failed to dlclose %s") ": %s\n",
		        entry->dsofile, strerror(errno));
	}
	afree(entry->dsofile, strlen(entry->dsofile) + 1);
	afree(entry, sizeof(struct dso_entry));
}

/**
 * Load a dso file and return its dso_entry.
 *
 * @param dsofile the filename for the dso.
 * @returns the struct dso_entry for the loaded dsofile, or NULL on error.
 */
struct dso_entry *load(char *dsofile) {
	int r;
	struct astr *entry_f;
	struct dso_entry *entry;

	/* get the name of the entry function */
	entry_f = (struct astr *) arcp_load(&livec_opts.entry);
	if(entry_f == NULL) {
		perror(ERRORTEXT("Fatal: No entry name defined"));
		r = unlink(dsofile);
		if(r != 0) {
			fprintf(stderr,
			        ERRORTEXT("Failed to unlink %s") ": %s\n",
			        dsofile, strerror(errno));
		}
		exit(EXIT_FAILURE);
	}

	/* allocate and initialize the dso entry */
	entry = amalloc(sizeof(struct dso_entry));
	if(entry == NULL) {
		perror(ERRORTEXT("Failed to allocate memory"
		                 " for a new DSO entry"));
		goto error0;
	}

	arcp_region_init(entry,
	                 (void (*)(struct arcp_region *)) dso_entry_destroy);

	entry->dsofile = dsofile;

	/* clear dlerror */
	dlerror();

	/* do the actual dlopen */
	entry->dlhandle = dlopen(dsofile, RTLD_NOW|RTLD_LOCAL);
	if(entry->dlhandle == NULL) {
		char *error;
		error = dlerror();
		if(error == NULL) {
			error = strerror(errno);
		}
		fprintf(stderr, ERRORTEXT("Could not dlopen %s") ": %s\n",
		        dsofile, error);
		goto error1;
	}

	/* look up the entry function */
	entry->proc = (livec_proc) dlsym(entry->dlhandle, astr_cstr(entry_f));
	if(entry->proc == NULL) {
		fprintf(stderr,
		        ERRORTEXT("Could not find '%s' function in %s\n"),
		        astr_cstr(entry_f), dsofile);
		goto error2;
	}

	/* relink all the autolink functions */
	r = autolink_relink(entry);
	if(r != 0) {
		goto error2;
	}

	return entry;

error2:
	r = dlclose(entry->dlhandle);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Failed to dlclose %s") ": %s\n",
		        dsofile, strerror(errno));
	}
error1:
	afree(entry, sizeof(struct dso_entry));
error0:
	arcp_release(entry_f);
	return NULL;
}

