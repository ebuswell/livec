# configure.ac
# 
# Process this file with autoconf to produce a configure script.
#
# Copyright 2013 Evan Buswell
# 
# This file is part of Live C.
# 
# Live C is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2.
# 
# Live C is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Live C.  If not, see <http://www.gnu.org/licenses/>.

# define version information
m4_define(LIVEC_VERSION, [0.1])

AC_PREREQ([2.65])
AC_INIT([livec], LIVEC_VERSION, [ebuswell@gmail.com])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_SRCDIR([src/livec.c])
AC_CONFIG_HEADERS([src/config.h])

[LIVEC_VERSION]=LIVEC_VERSION
AC_SUBST([LIVEC_VERSION])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for language features.
AC_LANG([C])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
