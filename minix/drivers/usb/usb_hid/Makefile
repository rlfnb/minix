# Makefile for USB HID keyboard driver
PROG=	usb_hid
SRCS=	usb_hid.c hid_kbd.c
MAN=

FILES=		${PROG}.conf
FILESNAME=	${PROG}
FILESDIR=	/etc/system.conf.d

DPADD+=	${LIBINPUTDRIVER} ${LIBUSB} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-linputdriver -lusb -lsys -ltimers

.include <minix.service.mk>
