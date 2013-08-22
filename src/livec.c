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
#include <stdlib.h>
#include <argp.h>
#include <libgen.h>
#include <string.h>
#include <sys/inotify.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdbool.h>

#define PROGNAME "livec"
#define VERSION "0.1"

#define ERRORTEXT(text) "[0;31;40m" text "[0;37;40m"
#define PROCTEXT(text) "[0;36;40m" text "[0;37;40m"
#define SUCCESSTEXT(text) "[0;32;40m" text "[0;37;40m"

/**
 * The state structure. One of these is passed in to every call.
 */
struct state {
    int argc; /** Number of arguments intended for the called program. */
    char **argv; /** Arguments intended for the called program. */
    char *filename; /** The filename of the C file we're loading. */
    char *ldflags; /** Collected LDFLAGS. Modifiable. Should be properly malloc'ed and freed. */
    char *cflags; /** Collected CFLAGS. Modifiable. Should be properly malloc'ed and freed. */
    char *entry; /** Name of entry point. Modifiable. Should be properly malloc'ed and freed. */
    void *state; /** Generic pointer to some sort of state structure. */
};

/* These are resolved by the argp argument parser to automagically do
 * some stuff. */
const char *argp_program_version =
    PROGNAME " " VERSION;
const char *argp_program_bug_address =
    "<bugs@FIXME.com>";

/* This parses the options. It is called in order for each option string.
 */
static error_t argp_parse_opt(int key, char *arg, struct argp_state *state) {
    struct state *livec_state = (struct state *) state->input;
    switch(key) {
    case ARGP_KEY_ARG:
	watch_filename = arg;
	break;
    case ARGP_KEY_END:
	if(state->arg_num != 1) {
	    argp_usage(state);
	}
	break;
    default:
	return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static void loader_thread(FILE *dsoqueue) {
    void *state = NULL;
    int r;
    char dsofile[1024];
    while(fgets(dsofile, 1024, dsoqueue) != NULL) {
	if(dsofile[strlen(dsofile) - 1] == '\n') {
	    dsofile[strlen(dsofile) - 1] = '\0';
	}
	r = fprintf(stderr, PROCTEXT("Loading %s...\n"), dsofile);
	if(r < 0) {
	    perror("Failed to print status message");
	    break;
	}
	void *dlhandle = dlopen(dsofile, RTLD_NOW);
	if(dlhandle == NULL) {
	    fprintf(stderr, ERRORTEXT("Could not dlopen %s") ": %s", dsofile, strerror(errno));
	    continue;
	}
	int (*runf)(void **) = (int (*)(void **)) dlsym(dlhandle, "run");
	if(runf == NULL) {
	    fprintf(stderr, ERRORTEXT("Could not find 'run' function in %s\n"), dsofile);
	    continue;
	}
	r = runf(&state);
	if(r < 0) {
	    perror(ERRORTEXT("Return value indicated an error"));
	    continue;
	}
    }
    if(ferror(dsoqueue)) {
	perror(ERRORTEXT("Error when reading from DSO queue"));
	exit(EXIT_FAILURE);
    }
    r = fclose(dsoqueue);
    if(r != 0) {
	perror(ERRORTEXT("Could not close DSO queue"));
	exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

pid_t prog;

static FILE *spawn_prog() {
    int pipefd[2];
    int r;
    r = pipe(pipefd);
    if(r != 0) {
	perror(ERRORTEXT("Could not create DSO queue"));
	exit(EXIT_FAILURE);
    }

    if((prog = fork()) == 0) {
	/* child */
	r = close(pipefd[1]); /* close write */
	if(r != 0) {
	    perror(ERRORTEXT("Failed to close write side of DSO queue"));
	    exit(EXIT_FAILURE);
	}
	FILE *dsoqueue_read = fdopen(pipefd[0], "r");
	if(dsoqueue_read == NULL) {
	    perror(ERRORTEXT("Failed to fdopen DSO queue reader"));
	    exit(EXIT_FAILURE);
	}

	loader_thread(dsoqueue_read);

	exit(EXIT_SUCCESS);
    } else if(prog == -1) {
	perror(ERRORTEXT("Could not fork"));
	exit(EXIT_FAILURE);
    }

    /* parent */
    r = close(pipefd[0]); /* close read */
    if(r != 0) {
	perror(ERRORTEXT("Failed to close read side of DSO queue"));
	exit(EXIT_FAILURE);
    }
    FILE *dsoqueue_write = fdopen(pipefd[1], "w");
    if(dsoqueue_write == NULL) {
	perror(ERRORTEXT("Failed to fdopen DSO queue writer"));
	exit(EXIT_FAILURE);
    }
    return dsoqueue_write;
}

static char *compile(char *filename) {
    char *tmpdir = NULL;
    if((getuid() == geteuid())
       && (getgid() == getegid())) {
	tmpdir = getenv("TMPDIR");
	if(tmpdir == NULL) {
	    tmpdir = getenv("TMP");
	}
    }
    if(tmpdir == NULL) {
	tmpdir = "/tmp";
    }

    char *file = alloca(strlen(filename) + 1);
    strcpy(file, filename);
    file = basename(file);

    char *extension = strrchr(file, '.');
    if((extension == NULL)
       || (extension == file)) {
	extension = file + strlen(file);
    }

    char *dsofile = malloc(strlen(tmpdir)
			   + 1 /* "/" */
			   + extension - file
			   + 6 /* 'X's */
			   + 3 /* ".so" */
			   + 1);
    if(dsofile == NULL) {
	perror(ERRORTEXT("Failed to allocate memory for temporary DSO file name"));
	return NULL;
    }
    strcpy(dsofile, tmpdir);
    dsofile[strlen(tmpdir)] = '/';
    strcpy(dsofile + strlen(tmpdir) + 1, file);
    memset(dsofile + strlen(tmpdir) + 1 + (extension - file), 'X', 6);
    strcpy(dsofile + strlen(tmpdir) + 1 + (extension - file) + 6, ".so");

    int tmpfd = mkstemps(dsofile, 3);
    if(tmpfd < 0) {
	perror(ERRORTEXT("Failed to create temporary DSO file"));
	free(dsofile);
	return NULL;
    }
    close(tmpfd);

    char *compiletmpl = "gcc $CFLAGS%s%s -shared -fPIC -DPIC -Wall -Wextra -Werror -march=native -o %s %s";

    char *compilecmd = alloca(strlen(dsofile) + strlen(filename) + strlen(compiletmpl) - 8 + (ldflags == NULL ? 0 : (strlen(ldflags) + 4)) + 1);
    sprintf(compilecmd, compiletmpl, ldflags == NULL ? "" : " -Wl,", ldflags == NULL ? "" : ldflags, dsofile, filename);
    int r = fprintf(stderr, "%s\n", compilecmd);
    if(r < 0) {
	perror(ERRORTEXT("Failed to print out compilation command"));
	goto error;
    }
    int status = system(compilecmd);
    if((status < 0)
       || (! WIFEXITED(status))
       || (WEXITSTATUS(status) != EXIT_SUCCESS)) {
	goto error;
    }
    return dsofile;
error:
    unlink(dsofile);
    free(dsofile);
    return NULL;
}

void collapse_space(char *s) {
    bool lastspace = true;
    do {
	switch(*s) {
	case ' ':
	case '\t':
	case '\v':
	case '\n':
	case '\f':
	case '\r':
	    if(lastspace) {
		memmove(s, s + 1, strlen(s));
	    }
	    lastspace = true;
	    break;
	default:
	    lastspace = false;
	}
    } while(*s++ != '\0');
}

int main(int argc, char **argv) {
    int r;
    struct state state;

    char *cflags = getenv("CFLAGS");
    if(cflags != NULL) {
	cflags = strdup(cflags);
    }

    char *ldflags = getenv("LDFLAGS");
    if(ldflags != NULL) {
	ldflags = strdup(ldflags);
	if(ldflags == NULL) {
	    perror(ERRORTEXT("Could not strdup cflags"));
	    exit(EXIT_FAILURE);
	}
	collapse_space(ldflags);
    }

    struct argp_option opts[] = {
	{"entry", 'e', "function", 0, "Name of entry function", 0},
	{NULL, 'W', NULL, OPTION_HIDDEN, NULL, 0},
	{"-Wc,option", 0, NULL, OPTION_DOC, "Pass option directly to the compiler", 0},
	{"-Wl,option", 0, NULL, OPTION_DOC, "Pass option directly to the linker", 0},
	{NULL, 0, NULL, 0, NULL, 0}
    };

    struct argp argp = {
	argp_parse_opt,
	"filename [OPTIONS...]",
	"livec -- simple livecoding environment for c code.",
	NULL, NULL, NULL
    };
    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &state);

    umask(077);

    watch_filename = strdup(watch_filename);
    if(watch_filename == NULL) {
    	perror(ERRORTEXT("Failed to strdup filename"));
    	exit(EXIT_FAILURE);
    }

    char *dirbuf = strdup(watch_filename);
    if(dirbuf == NULL) {
    	perror(ERRORTEXT("Failed to strdup filename"));
    	exit(EXIT_FAILURE);
    }
    char *dir = dirname(dirbuf);

    char *file = strdup(watch_filename);
    if(file == NULL) {
    	perror(ERRORTEXT("Failed to strdup filename"));
    	exit(EXIT_FAILURE);
    }
    file = basename(file);

    int notify_fd = inotify_init();
    if(notify_fd < 0) {
	perror(ERRORTEXT("Failed to initialize inotify system"));
	exit(EXIT_FAILURE);
    }
    int dwatch = inotify_add_watch(notify_fd, dir, IN_CLOSE_WRITE|IN_MOVED_TO|IN_MOVE_SELF);
    if(dwatch <= 0) {
	fprintf(stderr, ERRORTEXT("Failed to add inotify watch for %s") ": %s\n", dir, strerror(errno));
	exit(EXIT_FAILURE);
    }

    FILE *dsoqueue = spawn_prog();
    bool modified = true;
    for(;;) {
	if(modified) {
	    r = fprintf(stderr, PROCTEXT("Compiling %s...\n"), watch_filename);
	    if(r < 0) {
		exit(EXIT_FAILURE);
	    }
	    char *dsofile = compile(watch_filename);
	    if(dsofile == NULL) {
		r = fprintf(stderr, ERRORTEXT("Compilation failed.\n"));
		if(r < 0) {
		    exit(EXIT_FAILURE);
		}
	    } else {	
		r = fprintf(stderr, SUCCESSTEXT("Compilation succeeded.\n"));
		if(r < 0) {
		    exit(EXIT_FAILURE);
		}
		r = fprintf(dsoqueue, "%s\n", dsofile);
		if(r > 0) {
		    r = fflush(dsoqueue);
		}
		if(r != 0) {
		    perror(ERRORTEXT("Failed to enqueue dsofile for consumption"));
		    unlink(dsofile);
		    exit(EXIT_FAILURE);
		}
	    }
	}
	modified = false;
	static uint8_t buf[sizeof(struct inotify_event) + NAME_MAX + 1];
	ssize_t len = read(notify_fd, buf, sizeof(struct inotify_event) + NAME_MAX + 1);
	if(len <= 0) {
	    perror(ERRORTEXT("read() of inotify event failed"));
	    exit(EXIT_FAILURE);
	}
	size_t i;
	for(i = 0; i <= len - sizeof(struct inotify_event);) {
	    struct inotify_event *event = (struct inotify_event *) &buf[i];
	    i += sizeof(struct inotify_event) + event->len;

	    /* printf("event {\n"); */
	    /* printf("    wd = %d", event->wd); */
	    /* if(event->wd == dwatch) { */
	    /* 	printf("(dwatch)\n"); */
	    /* } else { */
	    /* 	printf("\n"); */
	    /* } */
	    /* printf("    mask = "); */
	    /* bool last = false; */
	    /* uint32_t mask = event->mask; */
	    /* if(mask & IN_ACCESS) { */
	    /* 	mask ^= IN_ACCESS; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_ACCESS"); */
	    /* } */
	    /* if(mask & IN_ATTRIB) { */
	    /* 	mask ^= IN_ATTRIB; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_ATTRIB"); */
	    /* } */
	    /* if(mask & IN_CLOSE_WRITE) { */
	    /* 	mask ^= IN_CLOSE_WRITE; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_CLOSE_WRITE"); */
	    /* } */
	    /* if(mask & IN_CLOSE_NOWRITE) { */
	    /* 	mask ^= IN_CLOSE_NOWRITE; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_CLOSE_NOWRITE"); */
	    /* } */
	    /* if(mask & IN_CREATE) { */
	    /* 	mask ^= IN_CREATE; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_CREATE"); */
	    /* } */
	    /* if(mask & IN_DELETE) { */
	    /* 	mask ^= IN_DELETE; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_DELETE"); */
	    /* } */
	    /* if(mask & IN_DELETE_SELF) { */
	    /* 	mask ^= IN_DELETE_SELF; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_DELETE_SELF"); */
	    /* } */
	    /* if(mask & IN_MODIFY) { */
	    /* 	mask ^= IN_MODIFY; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_MODIFY"); */
	    /* } */
	    /* if(mask & IN_MOVE_SELF) { */
	    /* 	mask ^= IN_MOVE_SELF; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_MOVE_SELF"); */
	    /* } */
	    /* if(mask & IN_MOVED_FROM) { */
	    /* 	mask ^= IN_MOVED_FROM; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_MOVED_FROM"); */
	    /* } */
	    /* if(mask & IN_MOVED_TO) { */
	    /* 	mask ^= IN_MOVED_TO; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_MOVED_TO"); */
	    /* } */
	    /* if(mask & IN_OPEN) { */
	    /* 	mask ^= IN_OPEN; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_OPEN"); */
	    /* } */
	    /* if(mask & IN_IGNORED) { */
	    /* 	mask ^= IN_IGNORED; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_IGNORED"); */
	    /* } */
	    /* if(mask & IN_ISDIR) { */
	    /* 	mask ^= IN_ISDIR; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_ISDIR"); */
	    /* } */
	    /* if(mask & IN_Q_OVERFLOW) { */
	    /* 	mask ^= IN_Q_OVERFLOW; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_Q_OVERFLOW"); */
	    /* } */
	    /* if(mask & IN_UNMOUNT) { */
	    /* 	mask ^= IN_UNMOUNT; */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	last = true; */
	    /* 	printf("IN_UNMOUNT"); */
	    /* } */
	    /* if(mask) { */
	    /* 	if(last) { */
	    /* 	    printf("|"); */
	    /* 	} */
	    /* 	printf("%d", mask); */
	    /* } */
	    /* printf("\n"); */
	    /* printf("    cookie: %d\n", event->cookie); */
	    /* printf("    len: %d\n", event->len); */
	    /* printf("    name: %s\n", event->name); */
	    /* printf("}\n"); */
	    /* continue; */

	    if(event->wd == dwatch) {
		if(event->mask & IN_MOVE_SELF) {
		    free(watch_filename);
		    free(dirbuf);
		    dirbuf = strdup(event->name);
		    if(dirbuf == NULL) {
			perror("Failed to strdup dir buffer");
			exit(EXIT_FAILURE);
		    }
		    dir = dirbuf;
		    watch_filename = malloc(strlen(dir) + 1 + strlen(file) + 1);
		    if(watch_filename == NULL) {
			perror(ERRORTEXT("Failed to allocate memory for moved file name"));
			exit(EXIT_FAILURE);
		    }
		    strcpy(watch_filename, dir);
		    watch_filename[strlen(dir)] = '/';
		    strcpy(watch_filename + strlen(dir) + 1, file);
		} else if((event->mask & (IN_CLOSE_WRITE|IN_MOVED_TO))
			  && (strcmp(event->name, file) == 0)) {
		    modified = true;
		}
	    }
	}
    }
}
