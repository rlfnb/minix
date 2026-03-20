# Makefile for ProcFS server
.include <bsd.own.mk>

PROG=	procfs
SRCS=	buf.c cpuinfo.c main.c pid.c root.c service.c tree.c util.c

CPPFLAGS+= -I${NETBSDSRCDIR}/minix
CPPFLAGS+= -I${NETBSDSRCDIR}/minix/fs
CPPFLAGS+= -I${NETBSDSRCDIR}/minix/servers

DPADD+=	${LIBVTREEFS} ${LIBFSDRIVER} ${LIBPCI}
LDADD+=	-lvtreefs -lfsdriver -lpci

.include <minix.service.mk>
