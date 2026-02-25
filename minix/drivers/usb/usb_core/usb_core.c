/*
 * USB Core - Central USB coordinator service
 *
 * This service replaces the DDEKit-based usbd with a native MINIX
 * process. It coordinates between HCD drivers and USB class drivers:
 *
 * - Accepts HCD registrations (uhci_hcd, ehci_hcd, etc.)
 * - Accepts class driver registrations (usb_hid, usb_storage, etc.)
 * - Routes URBs from class drivers to the correct HCD
 * - Handles device enumeration (via control transfers through HCDs)
 * - Announces new devices to matching class drivers
 *
 * No DDEKit, no user-space threading. Single sef_receive_status() loop.
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/usb.h>
#include <minix/usb_hcd.h>
#include <minix/usb_ch9.h>
#include <minix/safecopies.h>
#include <string.h>

#include "device.h"
#include "urb.h"

static int running = 1;


/*===========================================================================*
 *    handle_hcd_register                                                    *
 *===========================================================================*/
static void
handle_hcd_register(message *m)
{
	int hcd_id;
	message reply;

	hcd_id = dev_register_hcd(m->m_source,
		m->USB_HCD_PORTS, m->USB_HCD_CAPS);

	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_HCD_REGISTER_REPLY;
	reply.USB_HCD_ID = (hcd_id >= 0) ? hcd_id : -1;
	reply.USB_RESULT = (hcd_id >= 0) ? OK : ENOMEM;

	ipc_send(m->m_source, &reply);
}


/*===========================================================================*
 *    handle_hcd_port_status                                                 *
 *===========================================================================*/
static void
handle_hcd_port_status(message *m)
{
	int port = m->USB_HCD_PORT;
	int status = m->USB_HCD_PSTATUS;
	int speed = m->USB_HCD_SPEED;
	int hcd_index;
	struct usb_core_hcd *hcd;

	/* Find which HCD sent this message */
	for (hcd_index = 0; hcd_index < USB_MAX_HCDS; hcd_index++) {
		hcd = dev_get_hcd(hcd_index);
		if (hcd != NULL && hcd->ep == m->m_source)
			break;
	}

	if (hcd_index >= USB_MAX_HCDS) {
		printf("usb_core: port status from unknown HCD ep=%d\n",
			m->m_source);
		return;
	}

	if (status & USB_PORT_CONNECTED) {
		dev_port_connect(hcd_index, port, speed);
	} else {
		dev_port_disconnect(hcd_index, port);
	}
}


/*===========================================================================*
 *    handle_hcd_urb_complete                                                *
 *===========================================================================*/
static void
handle_hcd_urb_complete(message *m)
{
	urb_complete(m, m->m_source);
}


/*===========================================================================*
 *    handle_hcd_reset_done                                                  *
 *===========================================================================*/
static void
handle_hcd_reset_done(message *m)
{
	int port = m->USB_HCD_PORT;
	int speed = m->USB_HCD_SPEED;
	int hcd_index;
	struct usb_core_hcd *hcd;

	/* Find which HCD sent this */
	for (hcd_index = 0; hcd_index < USB_MAX_HCDS; hcd_index++) {
		hcd = dev_get_hcd(hcd_index);
		if (hcd != NULL && hcd->ep == m->m_source)
			break;
	}

	if (hcd_index >= USB_MAX_HCDS)
		return;

	printf("usb_core: port %d reset done (speed=%d)\n", port, speed);

	/* Device is now ready for enumeration.
	 * In a full implementation, we would start the enumeration
	 * sequence here (GET_DESCRIPTOR, SET_ADDRESS, etc.) by
	 * issuing control transfers through the HCD.
	 *
	 * For now, we create a device entry and announce it to
	 * class drivers, which can do their own setup via URBs. */
	dev_port_connect(hcd_index, port, speed);

	/* Find the device we just created and announce it */
	{
		int i;
		for (i = 1; i < USB_MAX_DEVICES; i++) {
			struct usb_core_device *dev = dev_find_by_id(i);
			if (dev != NULL &&
			    dev->hcd_index == hcd_index &&
			    dev->port == port) {
				/* Assign a USB address */
				int addr = dev_alloc_address();
				if (addr > 0) {
					dev->address = addr;
					dev->state = DEV_STATE_CONFIGURED;
				}
				/* Announce to class drivers */
				dev_announce_device(i);
				break;
			}
		}
	}
}


/*===========================================================================*
 *    handle_drv_register                                                    *
 *===========================================================================*/
static void
handle_drv_register(message *m)
{
	char name[M_PATH_STRING_MAX + 1];
	message reply;
	int r;

	strlcpy(name, m->USB_RB_INIT_NAME, sizeof(name));
	r = dev_register_driver(m->m_source, name);

	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_REPLY;
	reply.USB_RESULT = (r >= 0) ? 0 : ENOMEM;

	ipc_send(m->m_source, &reply);

	/* If there are already enumerated devices, announce them
	 * to the newly registered driver */
	if (r >= 0) {
		int i;
		for (i = 1; i < USB_MAX_DEVICES; i++) {
			struct usb_core_device *dev = dev_find_by_id(i);
			if (dev != NULL &&
			    dev->state == DEV_STATE_CONFIGURED &&
			    dev->driver_index < 0) {
				dev_announce_device(i);
			}
		}
	}
}


/*===========================================================================*
 *    handle_drv_submit_urb                                                  *
 *===========================================================================*/
static void
handle_drv_submit_urb(message *m)
{
	urb_submit(m, m->m_source);
}


/*===========================================================================*
 *    handle_drv_cancel_urb                                                  *
 *===========================================================================*/
static void
handle_drv_cancel_urb(message *m)
{
	message reply;
	int r;

	r = urb_cancel(m->USB_URB_ID, m->m_source);

	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_REPLY;
	reply.USB_RESULT = r;

	ipc_send(m->m_source, &reply);
}


/*===========================================================================*
 *    handle_drv_send_info                                                   *
 *===========================================================================*/
static void
handle_drv_send_info(message *m)
{
	message reply;

	/* Currently a no-op, kept for protocol compatibility */
	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_REPLY;
	reply.USB_RESULT = OK;

	ipc_send(m->m_source, &reply);
}


/*===========================================================================*
 *    usb_core_init                                                          *
 *===========================================================================*/
static int
usb_core_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	dev_init();
	urb_init();

	printf("usb_core: initialized\n");
	return OK;
}


/*===========================================================================*
 *    usb_core_startup                                                       *
 *===========================================================================*/
static void
usb_core_startup(void)
{
	sef_setcb_init_fresh(usb_core_init);
	sef_startup();
}


/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
int
main(void)
{
	message m;
	int r, ipc_status;

	usb_core_startup();

	while (running) {
		r = sef_receive_status(ANY, &m, &ipc_status);
		if (r != OK) {
			if (r == EINTR)
				continue;
			panic("usb_core: receive failed: %d", r);
		}

		/* Handle notifications */
		if (is_ipc_notify(ipc_status)) {
			switch (_ENDPOINT_P(m.m_source)) {
			case CLOCK:
				/* Timer expiry - could be used for
				 * enumeration timeouts */
				break;
			default:
				break;
			}
			continue;
		}

		/* Handle messages */
		switch (m.m_type) {

		/* Messages from HCDs */
		case USB_HCD_REGISTER:
			handle_hcd_register(&m);
			break;
		case USB_HCD_PORT_STATUS:
			handle_hcd_port_status(&m);
			break;
		case USB_HCD_URB_COMPLETE:
			handle_hcd_urb_complete(&m);
			break;
		case USB_HCD_RESET_DONE:
			handle_hcd_reset_done(&m);
			break;

		/* Messages from class drivers */
		case USB_RQ_INIT:
			handle_drv_register(&m);
			break;
		case USB_RQ_DEINIT:
			/* Driver deregistration - TODO */
			break;
		case USB_RQ_SEND_URB:
			handle_drv_submit_urb(&m);
			break;
		case USB_RQ_CANCEL_URB:
			handle_drv_cancel_urb(&m);
			break;
		case USB_RQ_SEND_INFO:
			handle_drv_send_info(&m);
			break;

		default:
			printf("usb_core: unknown message type %d "
				"from %d\n", m.m_type, m.m_source);
			break;
		}
	}

	return 0;
}
