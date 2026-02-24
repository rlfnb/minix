.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm" || ${MACHINE_ARCH} == "i386"
SUBDIR+=	usbd
.endif

SUBDIR+=	usb_storage usb_hub

.include <bsd.subdir.mk>
