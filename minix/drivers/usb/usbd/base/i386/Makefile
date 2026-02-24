# Makefile for the i386 USBD (UHCI-based)

PROG=		usbd
SRCS=		usbd.c usbd_x86.c hcd.c hcd_common.c hcd_ddekit.c hcd_schedule.c uhci_core.c uhci_pci.c
MAN=
BINDIR=		/service

.PATH:		${.CURDIR}/../ ${.CURDIR}/../../hcd ${.CURDIR}/../../hcd/uhci

CPPFLAGS+=	-I${.CURDIR}/../../include
CPPFLAGS+=	-I${.CURDIR}/../../hcd/uhci

DPADD+=		${LIBDDEKIT} ${LIBDDEKIT_USB_SERVER} ${LIBDEVMAN} ${LIBUSB} ${LIBMINC} ${LIBSYS}
LDADD+=		-lddekit -lddekit_usb_server -ldevman -lusb -lminc -lsys

.include <minix.service.mk>
