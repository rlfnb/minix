.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm" || ${MACHINE_ARCH} == "i386"
SUBDIR+=	usbd
.endif

# Native USB stack (DDEKit-free)
.if ${MACHINE_ARCH} == "i386"
SUBDIR+=	usb_core
SUBDIR+=	uhci_hcd
.endif
SUBDIR+=	usb_hid

SUBDIR+=	usb_storage usb_hub

.include <bsd.subdir.mk>
