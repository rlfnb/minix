/*
 * USB Core - URB routing between class drivers and HCDs
 */

#ifndef _USB_CORE_URB_H_
#define _USB_CORE_URB_H_

#include <minix/ipc.h>
#include <minix/safecopies.h>
#include <minix/usb.h>

/* Maximum pending URBs */
#define URB_MAX_PENDING   64

/* Internal URB tracking entry */
struct urb_entry {
	int             active;
	unsigned int    urb_id;         /* ID returned to class driver */
	int             dev_id;         /* Target USB device */
	endpoint_t      driver_ep;      /* Submitting class driver */
	cp_grant_id_t   driver_grant;   /* Grant from class driver */
	size_t          driver_grant_sz;/* Grant size */

	endpoint_t      hcd_ep;         /* HCD that executes this URB */
	cp_grant_id_t   hcd_grant;      /* Grant we gave to HCD */

	/* Buffered URB data for routing */
	char            buf[1024];
	size_t          buf_size;
};

/* Initialize URB subsystem */
void urb_init(void);

/* Submit a URB from a class driver, route to correct HCD */
int urb_submit(message *m, endpoint_t driver_ep);

/* Handle URB completion from HCD */
void urb_complete(message *m, endpoint_t hcd_ep);

/* Cancel a pending URB */
int urb_cancel(unsigned int urb_id, endpoint_t driver_ep);

#endif /* !_USB_CORE_URB_H_ */
