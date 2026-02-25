/*
 * USB Core - Device registry and enumeration
 *
 * Manages the registry of HCDs, USB devices, and class drivers.
 * Handles device enumeration (address assignment, descriptor reading)
 * by issuing control transfers through the appropriate HCD.
 */

#include <minix/drivers.h>
#include <minix/usb.h>
#include <minix/usb_hcd.h>
#include <minix/usb_ch9.h>
#include <minix/safecopies.h>
#include <minix/ds.h>
#include <string.h>

#include "device.h"
#include "urb.h"

static struct usb_core_hcd    hcds[USB_MAX_HCDS];
static struct usb_core_device devices[USB_MAX_DEVICES];
static struct usb_core_driver drivers[USB_MAX_DRIVERS];

/* Address allocation bitmap: bit set = address in use */
static unsigned char addr_bitmap[USB_MAX_DEVICES / 8];

/*===========================================================================*
 *    dev_init                                                               *
 *===========================================================================*/
void
dev_init(void)
{
	memset(hcds, 0, sizeof(hcds));
	memset(devices, 0, sizeof(devices));
	memset(drivers, 0, sizeof(drivers));
	memset(addr_bitmap, 0, sizeof(addr_bitmap));

	/* Mark address 0 as reserved (default address) */
	addr_bitmap[0] |= 0x01;

	/* Mark all driver slots as inactive */
	{
		int i;
		for (i = 0; i < USB_MAX_DRIVERS; i++)
			drivers[i].active = 0;
		for (i = 0; i < USB_MAX_DEVICES; i++) {
			devices[i].state = DEV_STATE_FREE;
			devices[i].driver_index = -1;
		}
	}
}


/*===========================================================================*
 *    dev_register_hcd                                                       *
 *===========================================================================*/
int
dev_register_hcd(endpoint_t ep, int num_ports, unsigned int caps)
{
	int i;

	for (i = 0; i < USB_MAX_HCDS; i++) {
		if (!hcds[i].active) {
			hcds[i].ep = ep;
			hcds[i].num_ports = num_ports;
			hcds[i].caps = caps;
			hcds[i].active = 1;

			printf("usb_core: HCD registered (ep=%d, ports=%d, "
				"caps=0x%x)\n", ep, num_ports, caps);
			return i;
		}
	}

	printf("usb_core: no free HCD slot\n");
	return -1;
}


/*===========================================================================*
 *    dev_register_driver                                                    *
 *===========================================================================*/
int
dev_register_driver(endpoint_t ep, const char *name)
{
	int i;

	for (i = 0; i < USB_MAX_DRIVERS; i++) {
		if (!drivers[i].active) {
			drivers[i].ep = ep;
			drivers[i].active = 1;
			strlcpy(drivers[i].name, name,
				sizeof(drivers[i].name));

			printf("usb_core: driver '%s' registered (ep=%d)\n",
				name, ep);
			return i;
		}
	}

	printf("usb_core: no free driver slot\n");
	return -1;
}


/*===========================================================================*
 *    dev_alloc_address                                                      *
 *===========================================================================*/
int
dev_alloc_address(void)
{
	int addr;

	/* USB addresses are 1-127 */
	for (addr = 1; addr < 128; addr++) {
		int byte = addr / 8;
		int bit = addr % 8;

		if (!(addr_bitmap[byte] & (1 << bit))) {
			addr_bitmap[byte] |= (1 << bit);
			return addr;
		}
	}

	return -1;  /* No free addresses */
}


/*===========================================================================*
 *    dev_port_connect                                                       *
 *===========================================================================*/
void
dev_port_connect(int hcd_index, int port, int speed)
{
	int i;

	/* Find a free device slot */
	for (i = 1; i < USB_MAX_DEVICES; i++) {
		if (devices[i].state == DEV_STATE_FREE) {
			devices[i].state = DEV_STATE_ENUMERATING;
			devices[i].hcd_index = hcd_index;
			devices[i].port = port;
			devices[i].speed = speed;
			devices[i].address = 0;
			devices[i].driver_index = -1;
			devices[i].num_interfaces = 0;
			devices[i].ep0_max_pkt =
				(speed == USB_SPEED_LOW) ? 8 : 64;

			printf("usb_core: device connected on HCD %d "
				"port %d (speed=%d, dev_id=%d)\n",
				hcd_index, port, speed, i);
			return;
		}
	}

	printf("usb_core: no free device slot\n");
}


/*===========================================================================*
 *    dev_port_disconnect                                                    *
 *===========================================================================*/
void
dev_port_disconnect(int hcd_index, int port)
{
	int i;

	for (i = 1; i < USB_MAX_DEVICES; i++) {
		if (devices[i].state != DEV_STATE_FREE &&
		    devices[i].hcd_index == hcd_index &&
		    devices[i].port == port) {

			/* Notify class driver if bound */
			if (devices[i].driver_index >= 0) {
				int di = devices[i].driver_index;
				message m;
				memset(&m, 0, sizeof(m));
				m.m_type = USB_WITHDRAW_DEV;
				m.USB_DEV_ID = i;
				(void)ipc_send(drivers[di].ep, &m);
			}

			/* Free the USB address */
			if (devices[i].address > 0) {
				int byte = devices[i].address / 8;
				int bit = devices[i].address % 8;
				addr_bitmap[byte] &= ~(1 << bit);
			}

			printf("usb_core: device %d disconnected "
				"(HCD %d port %d)\n",
				i, hcd_index, port);

			memset(&devices[i], 0, sizeof(devices[i]));
			devices[i].state = DEV_STATE_FREE;
			devices[i].driver_index = -1;
			return;
		}
	}
}


/*===========================================================================*
 *    dev_find_by_address                                                    *
 *===========================================================================*/
struct usb_core_device *
dev_find_by_address(int address)
{
	int i;

	for (i = 1; i < USB_MAX_DEVICES; i++) {
		if (devices[i].state != DEV_STATE_FREE &&
		    devices[i].address == address)
			return &devices[i];
	}
	return NULL;
}


/*===========================================================================*
 *    dev_find_by_id                                                         *
 *===========================================================================*/
struct usb_core_device *
dev_find_by_id(int dev_id)
{
	if (dev_id < 0 || dev_id >= USB_MAX_DEVICES)
		return NULL;
	if (devices[dev_id].state == DEV_STATE_FREE)
		return NULL;
	return &devices[dev_id];
}


/*===========================================================================*
 *    dev_get_hcd                                                            *
 *===========================================================================*/
struct usb_core_hcd *
dev_get_hcd(int index)
{
	if (index < 0 || index >= USB_MAX_HCDS)
		return NULL;
	if (!hcds[index].active)
		return NULL;
	return &hcds[index];
}


/*===========================================================================*
 *    dev_announce_device                                                    *
 *===========================================================================*/
void
dev_announce_device(int dev_id)
{
	struct usb_core_device *dev;
	int i;
	message m;

	dev = dev_find_by_id(dev_id);
	if (dev == NULL)
		return;

	/* Send USB_ANNOUCE_DEV to all registered class drivers.
	 * In a full implementation, we would match by interface class.
	 * For now, announce to all drivers and let them decide. */
	for (i = 0; i < USB_MAX_DRIVERS; i++) {
		if (!drivers[i].active)
			continue;

		memset(&m, 0, sizeof(m));
		m.m_type = USB_ANNOUCE_DEV;
		m.USB_DEV_ID = dev_id;
		m.USB_INTERFACES = dev->num_interfaces;

		if (ipc_send(drivers[i].ep, &m) == OK) {
			dev->driver_index = i;
			printf("usb_core: announced dev %d to '%s'\n",
				dev_id, drivers[i].name);
			return;
		}
	}

	printf("usb_core: no driver accepted device %d\n", dev_id);
}


/*===========================================================================*
 *    dev_get_driver_ep                                                      *
 *===========================================================================*/
endpoint_t
dev_get_driver_ep(int dev_id)
{
	struct usb_core_device *dev;

	dev = dev_find_by_id(dev_id);
	if (dev == NULL || dev->driver_index < 0)
		return NONE;
	return drivers[dev->driver_index].ep;
}
