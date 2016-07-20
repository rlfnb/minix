.include <bsd.own.mk>

# Makefile for SMBus support
PROG=	smbus
SRCS=	main.c 
FILES=$(PROG).conf
FILESNAME=$(PROG)
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBCHARDRIVER}
LDADD+=	-lchardriver

WARNS=	3

.include <minix.service.mk>
