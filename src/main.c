/* main.c initialization and configuration
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
#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <atomickit/rcp.h>
#include <atomickit/malloc.h>
#include <atomickit/string.h>

#include "livec.h"
#include "local.h"

/* These are resolved by the argp argument parser to automagically do
 * some stuff. */
const char *argp_program_version =
	PROGNAME " " VERSION;
const char *argp_program_bug_address =
	"<bugs@FIXME.com>";

/* command-line options */
static struct argp_option options[] = {
	{"entry", 'e', "function", 0, "Name of entry function", 0},
	{"compiler", 'c', "compiler", 0, "Specify compiler to use", 0},
	{NULL, 'W', "option", OPTION_HIDDEN, NULL, 0},
	{"-Wc,option", 0, NULL, OPTION_DOC,
	 "Pass option directly to the compiler", 0},
	{"-Wl,option", 0, NULL, OPTION_DOC,
	 "Pass option directly to the linker", 0},
	{NULL, 0, NULL, 0, NULL, 0}
};

/* defaults */
static struct astr default_compiler = {
	ARCP_REGION_STATIC_VAR_INIT(NULL),
	2,
	"cc"
};

static struct astr default_entry = {
	ARCP_REGION_STATIC_VAR_INIT(NULL),
	4,
	"main"
};

/* this will be set from the TMPDIR variable if it is available */
static char *default_builddir = "/tmp";

/* initialize options structure to NULL */
struct livec_opts livec_opts = {
	ARCP_VAR_INIT(NULL),
	ARCP_VAR_INIT(NULL),
	ARCP_VAR_INIT(NULL),
	ARCP_VAR_INIT(NULL),
	ARCP_VAR_INIT(NULL),
	ARCP_VAR_INIT(NULL)
};

/* utility function to collapse whitespace to a minimum; naïvely slow */
void str_collapse_ws(char *s) {
	bool lastspace;
	lastspace = true;
	while(*s != '\0') {
		switch(*s) {
		case ' ':
		case '\t':
		case '\v':
		case '\n':
		case '\f':
		case '\r':
			if(lastspace) {
				memmove(s, s + 1, strlen(s));
			} else {
				lastspace = true;
				*s++ = ' ';
			}
			if(*s == '\0') {
				*--s = '\0';
			}
			break;
		default:
			lastspace = false;
			s += 1;
		}
	}
}

/* This parses the options. It is called in order for each option string. */
static error_t argp_parse_opt(int key, char *arg, struct argp_state *pstate) {
	switch(key) {
	case 'e': { /* entry */
		struct astr *entry;
		if(arcp_load_phantom(&livec_opts.entry) != NULL) {
			/* only one entry can be defined */
			argp_usage(pstate);
		}
		entry = astr_cstrdup(arg);
		if(entry == NULL) {
			perror(ERRORTEXT("Fatal: failed to strdup entry"));
			exit(EXIT_FAILURE);
		}
		arcp_store(&livec_opts.entry, entry);
		arcp_release(entry);
		break;
	}
	case 'c': { /* compiler */
		struct astr *compiler;
		if(arcp_load_phantom(&livec_opts.compiler) != NULL) {
			/* only one compiler can be defined */
			argp_usage(pstate);
		}
		compiler = astr_cstrdup(arg);
		if(compiler == NULL) {
			perror(ERRORTEXT("Fatal: failed to strdup compiler"));
			exit(EXIT_FAILURE);
		}
		arcp_store(&livec_opts.compiler, compiler);
		arcp_release(compiler);
		break;
	}
	case 'W': { /* fake W option */
		char wtype;
		switch(wtype = *arg++) {
		case 'c':
		case 'l': { /* compiler/linker option */
			char *comma;
			struct astr *flags;
			struct astr *newflags;
			if(*arg++ != ',') {
				/* the c/l needs to be followed by a comma */
				argp_usage(pstate);
			}
			/* transform commas to spaces */
			comma = arg;
			while((comma = strchr(comma, ',')) != NULL) {
				*comma = ' ';
			}
			/* collapse whitespace */
			str_collapse_ws(arg);
			/* append to appropriate string */
			if(wtype == 'l') {
				flags = (struct astr *) arcp_load_phantom(
						&livec_opts.ldflags);
			} else {
				flags = (struct astr *) arcp_load_phantom(
						&livec_opts.cflags);
			}

			if(flags != NULL) {
				/* appending */
				newflags = astr_alloc(astr_len(flags)
				                      + strlen(arg) + 1);
				if(newflags == NULL) {
					perror(ERRORTEXT("Fatal: unable to"
					                 " allocate memory"
					                 " for compiler"
					                 " arguments"));
					exit(EXIT_FAILURE);
				}
				astr_cpy(newflags, flags);
				astr_cstrcat(newflags, " ");
				astr_cstrcat(newflags, arg);
			} else {
				/* replacing */
				newflags = astr_cstrdup(arg);
				if(newflags == NULL) {
					perror(ERRORTEXT("Fatal: unable to"
					                 " allocate memory"
					                 " for compiler"
					                 " arguments"));
					exit(EXIT_FAILURE);
				}
			}
			if(wtype == 'l') {
				arcp_store(&livec_opts.ldflags, newflags);
			} else {
				arcp_store(&livec_opts.cflags, newflags);
			}
			arcp_release(newflags);
			break;
		}
		default:
			/* the fake -W is catching something totally
 			 * invalid */
			return ARGP_ERR_UNKNOWN;
		}
		break;
	}
	case ARGP_KEY_ARG: { /* filename */
		struct astr *filename;
		if(arcp_load_phantom(&livec_opts.filename) != NULL) {
			/* more than one filename not yet supported */
			return ARGP_ERR_UNKNOWN;
		}
		filename = astr_cstrdup(arg);
		if(filename == NULL) {
			perror(ERRORTEXT("Fatal: failed to astr_cstrdup"
			                 " filename"));
			exit(EXIT_FAILURE);
		}
		arcp_store(&livec_opts.filename, filename);
		arcp_release(filename);
		break;
	}
	case ARGP_KEY_ARGS:
		/* everything after the "--" */
		args.argv = pstate->argv + pstate->next;
		args.argc = pstate->argc - pstate->next;
		break;
	case ARGP_KEY_END:
		if(pstate->arg_num < 1) {
			/* we at least need the filename argument */
			argp_usage(pstate);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

/* automagic argp struct */
static struct argp argp = {
	options,
	argp_parse_opt,
	"filename [OPTIONS...]",
	"livec -- simple livecoding environment for c code.",
	NULL, NULL, NULL
};

/* parse environmental, passed in, and default options */
static void parse_opts(int argc, char **argv) {
	char *tmpdir;
	char *cflags;
	char *ldflags;
	struct astr *sbuilddir;
	struct astr *scflags;
	struct astr *sldflags;

	/* process environment variables */
	tmpdir = getenv("TMPDIR");
	cflags = getenv("CFLAGS");
	ldflags = getenv("LDFLAGS");

	printf("cflags: \"%s\"\n", cflags);
	printf("ldflags: \"%s\"\n", ldflags);

	/* default tmpdir */
	if(tmpdir == NULL) {
		tmpdir = default_builddir;
	}

	sbuilddir = astr_cstrdup(tmpdir);
	if(sbuilddir == NULL) {
		perror(ERRORTEXT("Failed to strdup tmpdir"));
		exit(EXIT_FAILURE);
	}
	arcp_store(&livec_opts.builddir, sbuilddir);
	arcp_release(sbuilddir);

	if(cflags != NULL) {
		scflags = astr_cstrdup(cflags);
		if(scflags == NULL) {
			perror(ERRORTEXT("Failed to strdup cflags"));
			exit(EXIT_FAILURE);
		}
		arcp_store(&livec_opts.cflags, scflags);
		arcp_release(scflags);
	}

	if(ldflags != NULL) {
		sldflags = astr_cstrdup(ldflags);
		if(sldflags == NULL) {
			perror(ERRORTEXT("Failed to strdup ldflags"));
			exit(EXIT_FAILURE);
		}
		arcp_store(&livec_opts.ldflags, sldflags);
		arcp_release(sldflags);
	}

	/* process commandline variables */
	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, NULL);

	/* load in a few necessary defaults */
	if(arcp_load_phantom(&livec_opts.compiler) == NULL) {
		arcp_store(&livec_opts.compiler, &default_compiler);
	}

	if(arcp_load_phantom(&livec_opts.entry) == NULL) {
		arcp_store(&livec_opts.entry, &default_entry);
	}
}

pthread_t main_thread __attribute__((visibility("hidden")));

/* don't clutter the space */
int main(int argc, char **argv) __attribute__((visibility("hidden")));

int main(int argc, char **argv) {
	int r;

	parse_opts(argc, argv);

	/* create the thread-specific storage key for dso_entry */
	r = pthread_key_create(&entry_key, (void (*)(void *)) arcp_release);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Fatal: Failed to create"
		                          " thread-specific storage key for"
					  " dso_entry") ": %s\n",
		                          strerror(r));
		exit(EXIT_FAILURE);
	}

	umask(077);

	main_thread = pthread_self();

	/* set up signal catching */
	setup_signal_handling();

	watch_file();

	return 0;
}
