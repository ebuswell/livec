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
#include <atomickit/atomic-rcp.h>
#include <atomickit/atomic-malloc.h>
#include <atomickit/atomic-array.h>
#include <atomickit/atomic-string.h>
#include <witch.h>

#include "livec.h"
#include "local.h"

static arcp_t autolink_set = ARCP_VAR_INIT(NULL);

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
    afptr_free_dispatch_f(entry->dispatch_fptr);
    arcp_release(entry->fname);
    afree(entry, sizeof(struct alink_entry));
}

static struct alink_entry *alink_entry_create(char *fname, char *signature) {
    struct alink_entry *entry;
    struct astr *afname;
    afname = astr_cstrdup(fname);
    if(afname == NULL) {
	goto undo0;
    }
    entry = amalloc(sizeof(struct alink_entry));
    if(entry == NULL) {
	goto undo1;
    }
    arcp_region_init(entry, (void (*)(struct arcp_region *)) alink_entry_destroy);
    entry->fname = afname;
    arcp_init(&entry->afptr, NULL);
    printf("signature: %s\n", signature);
    entry->dispatch_fptr = afptr_create_dispatch_f(&entry->afptr, signature);
    if(entry->dispatch_fptr == NULL) {
	goto undo2;
    }

    return entry;

undo2:
    afree(entry, sizeof(struct alink_entry));
undo1:
    arcp_release(afname);
undo0:
    return NULL;
}

void *autolink_create(void *fptr, char *fname, char *signature) {
    struct alink_entry *entry;
    struct dso_afptr *afptr;
    struct dso_entry *dso_entry;
    struct aary *entry_set;
    struct aary *new_entry_set;
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

    entry = alink_entry_create(fname, signature);
    if(entry == NULL) {
	arcp_release(afptr);
	return NULL;
    }

    arcp_store(&entry->afptr, afptr);
    arcp_release(afptr);

    /* Add entry to entry set */
    do {
	entry_set = (struct aary *) arcp_load(&autolink_set);
	if(entry_set == NULL) {
	    new_entry_set = aary_create(1);
	    aary_storefirst(new_entry_set, entry);
	    if(new_entry_set == NULL) {
		arcp_release(entry);
		return NULL;
	    }
	} else {
	    size_t i, u, l;
	    int cmp;
	    u = aary_length(entry_set);
	    i = l = 0;
	    while(l < u) {
		i = (l + u) / 2;
		cmp = astr_cmp(entry->fname, ((struct alink_entry *) aary_load_phantom(entry_set, i))->fname);
		if(cmp < 0) {
		    u = i;
		} else if(cmp > 0) {
		    l = ++i;
		} else {
		    errno = EINVAL;
		    arcp_release(entry_set);
		    arcp_release(entry);
		    return NULL;
		}
	    }
	    new_entry_set = aary_dup_insert(entry_set, i, entry);
	    if(new_entry_set == NULL) {
		arcp_release(entry_set);
		arcp_release(entry);
		return NULL;
	    }
	}
    } while(!arcp_compare_store_release(&autolink_set, entry_set, new_entry_set));

    ret = entry->dispatch_fptr;
    arcp_release(entry);
    return ret;
}

int autolink_destroy(char *fname) {
    struct aary *entry_set;
    struct aary *new_entry_set;

    /* Remove entry from entry set */
    do {
	entry_set = (struct aary *) arcp_load(&autolink_set);
	if(entry_set == NULL) {
	    errno = EINVAL;
	    return -1;
	} else {
	    size_t i, u, l;
	    int cmp;
	    u = aary_length(entry_set);
	    l = 0;
	    while(l < u) {
		i = (l + u) / 2;
		cmp = strcmp(fname, astr_cstr(((struct alink_entry *) aary_load_phantom(entry_set, i))->fname));
		if(cmp < 0) {
		    u = i;
		} else if(cmp > 0) {
		    l = ++i;
		} else {
		    new_entry_set = aary_dup_remove(entry_set, i);
		    if(new_entry_set == NULL) {
			arcp_release(entry_set);
			return -1;
		    }
		    continue;
		}
	    }
	    errno = EINVAL;
	    arcp_release(entry_set);
	    return -1;
	}
    } while(!arcp_compare_store_release(&autolink_set, entry_set, new_entry_set));

    return 0;
}

static int autolink_relink(struct dso_entry *dso_entry) {
    int ret = 0;
    int i, len;
    struct alink_entry *entry;
    struct aary *entry_set;
    void *fptr;
    struct dso_afptr *afptr;

    entry_set = (struct aary *) arcp_load(&autolink_set);
    if(entry_set == NULL) {
	return 0;
    }

    len = aary_length(entry_set);

    for(i = 0; i < len; i++) {
	entry = (struct alink_entry *) aary_load_phantom(entry_set, i);
	fptr = dlsym(dso_entry->dlhandle, astr_cstr(entry->fname));
	if(fptr == NULL) {
	    fprintf(stderr, ERRORTEXT("Could not find '%s' function in %s\n"), astr_cstr(entry->fname), dso_entry->dsofile);
	    continue;
	}
	afptr = dso_afptr_create(dso_entry, fptr);
	if(afptr == NULL) {
	    fprintf(stderr, ERRORTEXT("Could not create afptr for function '%s'\n"), astr_cstr(entry->fname));
	    ret = -1;
	    continue;
	}
	arcp_store(&entry->afptr, afptr);
    }
    return ret;
}

static void dso_entry_destroy(struct dso_entry *entry) {
    int r;
    r = unlink(entry->dsofile);
    if(r != 0) {
	fprintf(stderr, ERRORTEXT("Failed to unlink %s") ": %s\n", entry->dsofile, strerror(errno));
    }
    r = dlclose(entry->dlhandle);
    if(r != 0) {
	fprintf(stderr, ERRORTEXT("Failed to dlclose %s") ": %s\n", entry->dsofile, strerror(errno));
    }
    afree(entry->dsofile, strlen(entry->dsofile) + 1);
    afree(entry, sizeof(struct dso_entry));
}

struct dso_entry *load(char *dsofile) {
    int r;
    struct astr *entry_f;
    struct dso_entry *entry;

    entry_f = (struct astr *) arcp_load(&livec_opts.entry);
    if(entry_f == NULL) {
	perror(ERRORTEXT("Fatal: No entry name defined"));
	r = unlink(dsofile);
	if(r != 0) {
	    fprintf(stderr, ERRORTEXT("Failed to unlink %s") ": %s\n", dsofile, strerror(errno));
	}
	exit(EXIT_FAILURE);
    }

    entry = amalloc(sizeof(struct dso_entry));
    if(entry == NULL) {
	perror(ERRORTEXT("Failed to allocate memory for a new DSO entry"));
	goto error0;
    }

    arcp_region_init(entry, (void (*)(struct arcp_region *)) dso_entry_destroy);

    entry->dsofile = dsofile;

    entry->dlhandle = dlopen(dsofile, RTLD_NOW);
    if(entry->dlhandle == NULL) {
	fprintf(stderr, ERRORTEXT("Could not dlopen %s") ": %s\n", dsofile, strerror(errno));
	goto error1;
    }

    entry->proc = (livec_proc) dlsym(entry->dlhandle, astr_cstr(entry_f));
    if(entry->proc == NULL) {
	fprintf(stderr, ERRORTEXT("Could not find '%s' function in %s\n"), astr_cstr(entry_f), dsofile);
	goto error2;
    }

    r = autolink_relink(entry);
    if(r != 0) {
	goto error2;
    }

    return entry;

error2:
    r = dlclose(entry->dlhandle);
    if(r != 0) {
	fprintf(stderr, ERRORTEXT("Failed to dlclose %s") ": %s\n", dsofile, strerror(errno));
    }
error1:
    afree(entry, sizeof(struct dso_entry));
error0:
    arcp_release(entry_f);
    return NULL;
}

