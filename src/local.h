/* misc.h Miscellaneous common stuff
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

#define PROGNAME "livec"
#define VERSION "0.1"

#define ERRORTEXT(text) "[0;31;40m" text "[0;37;40m"
#define PROCTEXT(text) "[0;36;40m" text "[0;37;40m"
#define SUCCESSTEXT(text) "[0;32;40m" text "[0;37;40m"

void str_collapse_ws(char *s) __attribute__((visibility("hidden")));
char *compile(struct astr *sfilename) __attribute__((visibility("hidden")));
struct dso_entry *load(char *dsofile) __attribute__((visibility("hidden")));
void watch_file(void) __attribute__((visibility("hidden")));
void run(struct dso_entry *entry) __attribute__((visibility("hidden")));

struct argstruct {
    int argc;
    char **argv;
};

extern struct argstruct args __attribute__((visibility("hidden")));
