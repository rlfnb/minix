/*
 * USB Core - Device registry and enumeration
 */

#ifndef _USB_CORE_DEVICE_H_
#define _USB_CORE_DEVICE_H_

#include <minix/usb_hcd.h>
#include <minix/usb_ch9.h>
#include <minix/ipc.h>
#include <minix/safecopies.h>

/* Device states */
#define DEV_STATE_FREE          0
#define DEV_STATE_ENUMERATING   1
#define DEV_STATE_ADDRESSED     2
#define DEV_STATE_CONFIGURED    3

/* HCD entry in the registry */
struct usb_core_hcd {
	endpoint_t      ep;             /* IPC endpoint */
	int             num_ports;
	unsigned int    caps;
	int             active;
};

/* Device entry in the registry */
struct usb_core_device {
	int             state;
	int             hcd_index;      /* Which HCD owns this device */
	int             port;           /* Root hub port number */
	int             address;        /* USB device address (1-127) */
	int             speed;          /* USB_SPEED_LOW/FULL/HIGH */

	usb_device_descriptor_t dev_desc;
	int             num_interfaces;
	int             driver_index;   /* Bound class driver, or -1 */

	/* Max packet size for EP0 (from device descriptor) */
	int             ep0_max_pkt;
};

/* Class driver entry */
struct usb_core_driver {
	endpoint_t      ep;
	int             active;
	char            name[32];
};

/* Initialize device subsystem */
void dev_init(void);

/* Register a new HCD */
int dev_register_hcd(endpoint_t ep, int num_ports, unsigned int caps);

/* Handle port status change from HCD */
void dev_port_connect(int hcd_index, int port, int speed);
void dev_port_disconnect(int hcd_index, int port);

/* Register a class driver */
int dev_register_driver(endpoint_t ep, const char *name);

/* Find a free USB address */
int dev_alloc_address(void);

/* Lookup device by address */
struct usb_core_device *dev_find_by_address(int address);

/* Lookup device by dev_id (index) */
struct usb_core_device *dev_find_by_id(int dev_id);

/* Lookup HCD by index */
struct usb_core_hcd *dev_get_hcd(int index);

/* Announce device to matching class driver */
void dev_announce_device(int dev_id);

/* Get driver endpoint for a device */
endpoint_t dev_get_driver_ep(int dev_id);

#endif /* !_USB_CORE_DEVICE_H_ */
