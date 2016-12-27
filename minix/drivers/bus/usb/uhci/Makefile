# Makefile for UHCI support
MAN=
PROG=uhci
SRCS=uhci.c

FILES=$(PROG).conf
FILESNAME=$(PROG)
FILESDIR= /etc/system.conf.d

DPADD+=${LIBSYS}
LDADD+= -lsys

WARNS=3

.include <minix.service.mk>
