/*
 * x86 (i386) USBD setup
 *
 * Platform-specific implementation of usbd_init_hcd() and usbd_deinit_hcd()
 * for x86 systems. This initializes the UHCI host controller driver,
 * which is a PCI-based USB 1.1 controller commonly found on x86 platforms.
 */

#include <usbd/hcd_platforms.h>
#include <usbd/usbd_common.h>
#include <usbd/usbd_interface.h>


/*===========================================================================*
 *    usbd_init_hcd                                                          *
 *===========================================================================*/
int
usbd_init_hcd(void)
{
	DEBUG_DUMP;

	USB_MSG("Initializing UHCI host controller driver");

	return uhci_pci_init();
}


/*===========================================================================*
 *    usbd_deinit_hcd                                                        *
 *===========================================================================*/
void
usbd_deinit_hcd(void)
{
	DEBUG_DUMP;

	uhci_pci_deinit();
}
