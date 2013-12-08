PREFIX?=/usr/local
INCLUDEDIR?=${PREFIX}/include
BINDIR?=${PREFIX}/lib
DESTDIR?=

CC?=gcc
CFLAGS?=-Og -g3
LDFLAGS?=
AR?=ar
ARFLAGS?=rv

ATOMICKIT_CFLAGS!=pkg-config --cflags atomickit
ATOMICKIT_LIBS!=pkg-config --libs atomickit
ATOMICKIT_STATIC!=pkg-config --static atomickit
LIBWITCH_CFLAGS!=pkg-config --cflags libwitch
LIBWITCH_LIBS!=pkg-config --libs libwitch
LIBWITCH_STATIC!=pkg-config --static libwitch

CFLAGS+=${ATOMICKIT_CFLAGS} ${LIBWITCH_CFLAGS}
CFLAGS+=-Wall -Wextra -Wmissing-prototypes -Wredundant-decls -Wdeclaration-after-statement
CFLAGS+=-fplan9-extensions
CFLAGS+=-Iinclude

LIBS=-ldl -lpthread ${ATOMICKIT_LIBS} ${LIBWITCH_LIBS}
STATIC=${ATOMICKIT_STATIC} ${LIBWITCH_STATIC}
