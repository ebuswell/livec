.PHONY: static all install-headers install-livec install-livec-static \
        install-static install install install-strip uninstall clean check

.SUFFIXES: .o

include config.mk

VERSION=0.1

SRCS=src/livec.c src/compile.c src/link.c src/main.c src/run.c
HEADERS=include/livec.h

OBJS=${SRCS:.c=.o}

all: livec

.c.o:
	${CC} ${CFLAGS} -c $< -o $@

livec: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${OBJS} ${LIBS} -o livec

livec-static: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -static ${OBJS} ${STATIC} -o livec-static

static: livec-static

install-headers:
	(umask 022; mkdir -p ${DESTDIR}${INCLUDEDIR})
	install -m 644 -t ${DESTDIR}${INCLUDEDIR} ${HEADERS}

install-livec: livec
	(umask 022; mkdir -p ${DESTDIR}${BINDIR})
	install -m 755 livec ${DESTDIR}${BINDIR}/livec

install-livec-static: livec-static
	(umask 022; mkdir -p ${DESTDIR}${BINDIR})
	install -m 755 livec-static ${DESTDIR}${BINDIR}/livec

install-livec-strip: install-livec
	strip -g ${DESTDIR}${BINDIR}/livec

install-livec-static-strip: install-livec-static
	strip -g ${DESTDIR}${BINDIR}/livec

install-static: static install-static install-headers

install: shared install-livec install-headers install-pkgconfig

install-strip: install install-livec-strip

install-static-strip: install-static install-livec-static-strip

uninstall: 
	rm -f ${DESTDIR}${BINDIR}/livec
	rm -f ${DESTDIR}${INCLUDEDIR}/livec.h

clean:
	rm -f livec
	rm -f livec-static
	rm -f ${OBJS}

check:
	echo "No check defined yet"
