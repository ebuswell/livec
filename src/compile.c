/* compile.c
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
#include <alloca.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <atomickit/rcp.h>
#include <atomickit/string.h>
#include <atomickit/malloc.h>

#include "livec.h"
#include "local.h"

static const char *compiletmpl
	= "%s -march=native %s %s -shared -fPIC -DPIC -o %s %s";

/**
 * (Re-)compile the file and return the temporarily allocated dso file.
 */
char *compile(struct astr *sfilename) {
	int r;
	struct astr *sbuilddir;
	struct astr *scompiler;
	struct astr *sldflags;
	struct astr *scflags;
	char *file;
	char *extension;
	char *cflags;
	char *ldflags;
	char *dsofile;
	char *compilecmd;

	/* load all the configuration options */
	sbuilddir = (struct astr *) arcp_load(&livec_opts.builddir);
	scompiler = (struct astr *) arcp_load(&livec_opts.compiler);
	sldflags = (struct astr *) arcp_load(&livec_opts.ldflags);
	scflags = (struct astr *) arcp_load(&livec_opts.cflags);

	if(sbuilddir == NULL) {
		fprintf(stderr,
		        ERRORTEXT("Fatal: No build directory defined\n"));
		exit(EXIT_FAILURE);
	}
	if(scompiler == NULL) {
		fprintf(stderr, ERRORTEXT("Fatal: No compiler defined\n"));
		exit(EXIT_FAILURE);
	}

	if(sldflags != NULL) {
		/* convert ldflags into a form suitable for passing directly to
 		 * the compiler */
		char *space;
		ldflags = alloca(astr_len(sldflags) + 1 + 4);
		strcpy(ldflags + 4, astr_cstr(sldflags));
		str_collapse_ws(ldflags + 4);
		ldflags[0] = '-';
		ldflags[1] = 'W';
		ldflags[2] = 'l';
		ldflags[3] = ',';
		/* transform spaces to commas */
		space = ldflags;
		while((space = strchr(space, ' ')) != NULL) {
			*space = ',';
		}
	} else {
		ldflags = "";
	}

	if(scflags == NULL) {
		cflags = "";
	} else {
		cflags = astr_cstr(scflags);
	}

	/* get a file name that has stripped off the directory part and the
 	 * extension */
	file = alloca(astr_len(sfilename) + 1);
	strcpy(file, astr_cstr(sfilename));
	file = basename(file);

	extension = strrchr(file, '.');
	if((extension != NULL)
	   && (extension != file)) {
		*extension++ = '\0';
	}

	/* create the template for the DSO file */
	dsofile = amalloc(astr_len(sbuilddir)
	                  + 1 /* "/" */
	                  + strlen(file)
	                  + 6 /* "XXXXXX" */
	                  + 3 /* ".so" */
	                  + 1);
	if(dsofile == NULL) {
		perror(ERRORTEXT("Failed to allocate memory for temporary DSO file name"));
		goto error1;
	}
	strcpy(dsofile, astr_cstr(sbuilddir));
	strcat(dsofile, "/");
	strcat(dsofile, file);
	strcat(dsofile, "XXXXXX");
	strcat(dsofile, ".so");

	/* create (touch) the DSO file from the template */
	{
		int tmpfd = mkstemps(dsofile, 3);
		if(tmpfd < 0) {
			perror(ERRORTEXT("Failed to create temporary DSO file"));
			goto error2;
		}
		close(tmpfd);
	}

	/* create the compile command from the template */
	compilecmd = alloca(strlen(compiletmpl) - 10 /* 10 is the length of the
							sprintf characters */
	                    + astr_len(scompiler)
	                    + strlen(ldflags)
	                    + strlen(cflags)
	                    + strlen(dsofile)
	                    + astr_len(sfilename)
	                    + 1);
	sprintf(compilecmd, compiletmpl,
	        astr_cstr(scompiler),
	        ldflags,
	        cflags,
	        dsofile,
	        astr_cstr(sfilename));
	/* print and run the compile command */
	fprintf(stderr, "%s\n", compilecmd);
	r = system(compilecmd);
	if((r < 0)
	   || (! WIFEXITED(r))
	   || (WEXITSTATUS(r) != EXIT_SUCCESS)) {
		goto error3;
	}
	arcp_release(sbuilddir);
	arcp_release(scompiler);
	arcp_release(sldflags);
	arcp_release(scflags);
	return dsofile;

error3:
	r = unlink(dsofile);
	if(r != 0) {
		fprintf(stderr, ERRORTEXT("Failed to unlink %s") ": %s\n", dsofile, strerror(errno));
	}
error2:
	afree(dsofile, strlen(dsofile) + 1);
error1:
	arcp_release(sbuilddir);
	arcp_release(scompiler);
	arcp_release(sldflags);
	arcp_release(scflags);
	return NULL;
}
