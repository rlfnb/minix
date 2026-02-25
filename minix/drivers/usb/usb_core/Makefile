# Makefile for USB Core coordinator service
PROG=	usb_core
SRCS=	usb_core.c device.c urb.c
MAN=

FILES=		${PROG}.conf
FILESNAME=	${PROG}
FILESDIR=	/etc/system.conf.d

DPADD+=	${LIBSYS}
LDADD+=	-lsys

.include <minix.service.mk>
