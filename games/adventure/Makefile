#	$NetBSD: Makefile,v 1.14 2013/02/16 16:30:28 jmcneill Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/12/93

PROG=	adventure
SRCS=	main.c init.c done.c save.c subr.c vocab.c wizard.c io.c data.c crc.c
MAN=	adventure.6
HIDEGAME=hidegame
CLEANFILES+=mkdata setup.o data.c

data.c: glorkz mkdata
	${_MKTARGET_CREATE}
	./mkdata ${.CURDIR}/glorkz > data.c

setup.o: setup.c hdr.h
	${HOST_CC:Ucc} ${HOST_CFLAGS} ${HOST_CPPFLAGS} -I${.CURDIR} -c -o ${.TARGET} ${.CURDIR}/setup.c
mkdata: setup.o
	${_MKTARGET_LINK}
	${HOST_LINK.c} -o ${.TARGET} ${.ALLSRC}

.include <bsd.prog.mk>
