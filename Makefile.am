# Makefile.am
# 
# Copyright 2013 Evan Buswell
# 
# This file is part of Live C.
# 
# Live C is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2.
# 
# Live C is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Live C.  If not, see <http://www.gnu.org/licenses/>.

MAINTAINERCLEANFILES = aclocal.m4 config.guess config.sub configure depcomp install-sh ltmain.sh Makefile.in missing src/config.h.in~ compile
EXTRA_DIST = autogen.sh
AUTOMAKE_OPTIONS = foreign serial-tests subdir-objects

bin_PROGRAMS = livec
livec_SOURCES = src/livec.c
livec_LDADD = -ldl
livec_LDFLAGS = $(LDFLAGS) -L$(libdir)
livec_CFLAGS = $(CFLAGS) -Og -g3 -Wall -Wextra -Wmissing-prototypes -Wredundant-decls
