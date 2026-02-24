# Makefile for usb host controllers
.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR= .WAIT base/earm
.elif ${MACHINE_ARCH} == "i386"
SUBDIR= .WAIT base/i386
.endif

.include <bsd.subdir.mk>
