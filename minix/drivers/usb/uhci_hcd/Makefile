# Makefile for standalone UHCI Host Controller Driver
PROG=	uhci_hcd
SRCS=	uhci_hcd.c uhci_hw.c
MAN=

FILES=		${PROG}.conf
FILESNAME=	${PROG}
FILESDIR=	/etc/system.conf.d

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

.include <minix.service.mk>
