/*
 * USB Core - URB routing
 *
 * Routes URBs from class drivers to the correct HCD and back.
 * Uses safecopy grants to transfer URB data between processes.
 */

#include <minix/drivers.h>
#include <minix/usb.h>
#include <minix/usb_hcd.h>
#include <minix/safecopies.h>
#include <string.h>

#include "urb.h"
#include "device.h"

static struct urb_entry urbs[URB_MAX_PENDING];
static unsigned int next_urb_id = 1;


/*===========================================================================*
 *    urb_init                                                               *
 *===========================================================================*/
void
urb_init(void)
{
	memset(urbs, 0, sizeof(urbs));
}


/*===========================================================================*
 *    find_free_urb                                                          *
 *===========================================================================*/
static struct urb_entry *
find_free_urb(void)
{
	int i;
	for (i = 0; i < URB_MAX_PENDING; i++) {
		if (!urbs[i].active)
			return &urbs[i];
	}
	return NULL;
}


/*===========================================================================*
 *    find_urb_by_id                                                         *
 *===========================================================================*/
static struct urb_entry *
find_urb_by_id(unsigned int urb_id)
{
	int i;
	for (i = 0; i < URB_MAX_PENDING; i++) {
		if (urbs[i].active && urbs[i].urb_id == urb_id)
			return &urbs[i];
	}
	return NULL;
}


/*===========================================================================*
 *    urb_submit                                                             *
 *===========================================================================*/
int
urb_submit(message *m, endpoint_t driver_ep)
{
	struct urb_entry *ue;
	struct usb_core_device *dev;
	struct usb_core_hcd *hcd;
	cp_grant_id_t grant;
	size_t grant_size;
	message reply;
	int r;

	grant = m->USB_GRANT_ID;
	grant_size = m->USB_GRANT_SIZE;

	/* Find a free URB slot */
	ue = find_free_urb();
	if (ue == NULL) {
		printf("usb_core: no free URB slots\n");
		memset(&reply, 0, sizeof(reply));
		reply.m_type = USB_REPLY;
		reply.USB_RESULT = ENOMEM;
		ipc_send(driver_ep, &reply);
		return ENOMEM;
	}

	/* Copy URB data from class driver via grant */
	if (grant_size > sizeof(ue->buf))
		grant_size = sizeof(ue->buf);

	r = sys_safecopyfrom(driver_ep, grant, 0,
		(vir_bytes)ue->buf, grant_size);
	if (r != OK) {
		printf("usb_core: safecopy from driver failed: %d\n", r);
		memset(&reply, 0, sizeof(reply));
		reply.m_type = USB_REPLY;
		reply.USB_RESULT = r;
		ipc_send(driver_ep, &reply);
		return r;
	}

	/* Extract device ID from the URB buffer.
	 * The URB struct starts with dev_id (after the 'next' pointer
	 * which was excluded from the grant). */
	{
		struct usb_urb *urb = (struct usb_urb *)
			((char *)ue->buf - offsetof(struct usb_urb, dev_id));
		/* Actually, the grant starts at dev_id, so buf[0..3] is dev_id */
		int dev_id;
		memcpy(&dev_id, ue->buf, sizeof(int));
		ue->dev_id = dev_id;
	}

	/* Look up the device and its HCD */
	dev = dev_find_by_id(ue->dev_id);
	if (dev == NULL) {
		printf("usb_core: URB for unknown device %d\n", ue->dev_id);
		memset(&reply, 0, sizeof(reply));
		reply.m_type = USB_REPLY;
		reply.USB_RESULT = ENODEV;
		ipc_send(driver_ep, &reply);
		return ENODEV;
	}

	hcd = dev_get_hcd(dev->hcd_index);
	if (hcd == NULL) {
		printf("usb_core: no HCD for device %d\n", ue->dev_id);
		memset(&reply, 0, sizeof(reply));
		reply.m_type = USB_REPLY;
		reply.USB_RESULT = ENODEV;
		ipc_send(driver_ep, &reply);
		return ENODEV;
	}

	/* Set up URB entry */
	ue->active = 1;
	ue->urb_id = next_urb_id++;
	if (next_urb_id == 0)
		next_urb_id = 1;
	ue->driver_ep = driver_ep;
	ue->driver_grant = grant;
	ue->driver_grant_sz = grant_size;
	ue->hcd_ep = hcd->ep;
	ue->buf_size = grant_size;

	/* Create a grant for the HCD to access our copy of the URB data */
	ue->hcd_grant = cpf_grant_direct(hcd->ep,
		(vir_bytes)ue->buf, grant_size,
		CPF_READ | CPF_WRITE);

	if (!GRANT_VALID(ue->hcd_grant)) {
		printf("usb_core: grant to HCD failed\n");
		ue->active = 0;
		memset(&reply, 0, sizeof(reply));
		reply.m_type = USB_REPLY;
		reply.USB_RESULT = ENOMEM;
		ipc_send(driver_ep, &reply);
		return ENOMEM;
	}

	/* Send reply to class driver with the URB ID */
	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_REPLY;
	reply.USB_RESULT = 0;
	reply.USB_URB_ID = ue->urb_id;
	r = ipc_send(driver_ep, &reply);
	if (r != OK) {
		printf("usb_core: reply to driver failed: %d\n", r);
		cpf_revoke(ue->hcd_grant);
		ue->active = 0;
		return r;
	}

	/* Forward URB to HCD */
	{
		message hcd_msg;
		memset(&hcd_msg, 0, sizeof(hcd_msg));
		hcd_msg.m_type = USB_HCD_SUBMIT_URB;
		hcd_msg.USB_GRANT_ID = ue->hcd_grant;
		hcd_msg.USB_GRANT_SIZE = grant_size;
		hcd_msg.USB_URB_ID = ue->urb_id;

		r = ipc_send(hcd->ep, &hcd_msg);
		if (r != OK) {
			printf("usb_core: send to HCD failed: %d\n", r);
			cpf_revoke(ue->hcd_grant);
			ue->active = 0;
			return r;
		}
	}

	return OK;
}


/*===========================================================================*
 *    urb_complete                                                           *
 *===========================================================================*/
void
urb_complete(message *m, endpoint_t hcd_ep)
{
	struct urb_entry *ue;
	unsigned int urb_id;
	int r;

	urb_id = m->USB_URB_ID;

	ue = find_urb_by_id(urb_id);
	if (ue == NULL) {
		printf("usb_core: completion for unknown URB %u\n", urb_id);
		return;
	}

	if (ue->hcd_ep != hcd_ep) {
		printf("usb_core: URB %u completed by wrong HCD\n", urb_id);
		return;
	}

	/* Copy completed URB data back to class driver via its grant */
	r = sys_safecopyto(ue->driver_ep, ue->driver_grant, 0,
		(vir_bytes)ue->buf, ue->buf_size);
	if (r != OK) {
		printf("usb_core: safecopy back to driver failed: %d\n", r);
	}

	/* Revoke grant to HCD */
	cpf_revoke(ue->hcd_grant);

	/* Notify class driver of completion */
	{
		message drv_msg;
		memset(&drv_msg, 0, sizeof(drv_msg));
		drv_msg.m_type = USB_COMPLETE_URB;
		drv_msg.USB_URB_ID = ue->urb_id;

		r = ipc_send(ue->driver_ep, &drv_msg);
		if (r != OK) {
			printf("usb_core: completion notify failed: %d\n", r);
		}
	}

	/* Free the URB entry */
	ue->active = 0;
}


/*===========================================================================*
 *    urb_cancel                                                             *
 *===========================================================================*/
int
urb_cancel(unsigned int urb_id, endpoint_t driver_ep)
{
	struct urb_entry *ue;

	ue = find_urb_by_id(urb_id);
	if (ue == NULL)
		return ESRCH;

	if (ue->driver_ep != driver_ep)
		return EPERM;

	/* Revoke grant and free entry */
	cpf_revoke(ue->hcd_grant);
	ue->active = 0;

	return OK;
}
