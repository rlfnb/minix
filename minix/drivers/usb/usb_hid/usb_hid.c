/*
 * USB HID Keyboard Driver for MINIX
 *
 * This driver bridges two frameworks:
 * - libinputdriver: for sending keyboard events to the input server
 * - libusb: for USB communication with usb_core
 *
 * The main loop uses inputdriver_task(), which calls sef_receive_status(ANY).
 * USB messages (device announcements, URB completions) arrive via the
 * idr_other callback, keeping everything in a single message loop.
 *
 * The driver uses USB HID Boot Protocol, which provides 8-byte reports
 * without needing to parse HID Report Descriptors. This is sufficient
 * for standard keyboards.
 */

#include <minix/drivers.h>
#include <minix/input.h>
#include <minix/inputdriver.h>
#include <minix/usb.h>
#include <minix/usb_hcd.h>
#include <minix/ds.h>
#include <minix/safecopies.h>
#include <minix/timers.h>
#include <string.h>

#include "hid_kbd.h"

/* Device state */
static int connected = 0;       /* Is a keyboard connected? */
static int device_id = -1;      /* USB device ID from usb_core */
static int polling = 0;         /* Is interrupt polling active? */

/* URB for interrupt IN transfers (keyboard reports) */
static struct usb_urb *poll_urb = NULL;
static char poll_urb_buf[USB_URBSIZE(HID_KBD_REPORT_SIZE, 0)];

/* Timer for polling retry */
static minix_timer_t poll_timer;
static int timer_running = 0;

/* USB driver callbacks */
static void usb_hid_connect(unsigned int dev_id, unsigned int interfaces);
static void usb_hid_disconnect(unsigned int dev_id);
static void usb_hid_urb_complete(struct usb_urb *urb);

static struct usb_driver usb_hid_driver = {
	.urb_completion    = usb_hid_urb_complete,
	.connect_device    = usb_hid_connect,
	.disconnect_device = usb_hid_disconnect,
};


/*===========================================================================*
 *    submit_interrupt_poll                                                   *
 *===========================================================================*/
static void
submit_interrupt_poll(void)
{
	if (!connected || polling)
		return;

	/* Set up the interrupt IN URB */
	poll_urb = (struct usb_urb *)poll_urb_buf;
	memset(poll_urb, 0, sizeof(poll_urb_buf));

	poll_urb->dev_id = device_id;
	poll_urb->type = USB_TRANSFER_INT;
	poll_urb->endpoint = 1;     /* Standard keyboard interrupt EP */
	poll_urb->direction = USB_IN;
	poll_urb->size = HID_KBD_REPORT_SIZE;
	poll_urb->urb_size = USB_URBSIZE(HID_KBD_REPORT_SIZE, 0);
	poll_urb->interval = 10;    /* Poll interval in ms */

	if (usb_send_urb(poll_urb) == 0) {
		polling = 1;
	} else {
		printf("usb_hid: failed to submit interrupt URB\n");
	}
}


/*===========================================================================*
 *    poll_timer_callback                                                    *
 *===========================================================================*/
static void
poll_timer_callback(int UNUSED(arg))
{
	timer_running = 0;
	submit_interrupt_poll();
}


/*===========================================================================*
 *    schedule_poll_retry                                                    *
 *===========================================================================*/
static void
schedule_poll_retry(void)
{
	if (timer_running || !connected)
		return;

	/* Retry in 100ms */
	set_timer(&poll_timer, sys_hz() / 10, poll_timer_callback, 0);
	timer_running = 1;
}


/*===========================================================================*
 *    USB driver callbacks                                                   *
 *===========================================================================*/

static void
usb_hid_connect(unsigned int dev, unsigned int interfaces)
{
	printf("usb_hid: device %u connected (%u interfaces)\n",
		dev, interfaces);

	device_id = dev;
	connected = 1;
	polling = 0;

	/* Start polling for keyboard reports */
	submit_interrupt_poll();
}

static void
usb_hid_disconnect(unsigned int dev)
{
	printf("usb_hid: device %u disconnected\n", dev);

	if ((int)dev == device_id) {
		connected = 0;
		polling = 0;
		device_id = -1;

		/* Reset keyboard state so all keys appear released */
		hid_kbd_init();
	}
}

static void
usb_hid_urb_complete(struct usb_urb *urb)
{
	polling = 0;

	if (!connected)
		return;

	if (urb->status == 0 &&
	    urb->actual_length >= HID_KBD_REPORT_SIZE) {
		/* Process the HID report */
		hid_kbd_process_report(
			(const uint8_t *)urb->buffer,
			urb->actual_length);
	}

	/* Submit next poll immediately (continuous polling) */
	submit_interrupt_poll();

	/* If submission failed, retry with timer */
	if (!polling)
		schedule_poll_retry();
}


/*===========================================================================*
 *    inputdriver callbacks                                                  *
 *===========================================================================*/

static void
usb_hid_set_leds(unsigned int leds)
{
	hid_kbd_set_leds(leds);

	/* TODO: Send SET_REPORT to keyboard via USB.
	 * This requires a control transfer URB to EP0 with:
	 *   bmRequestType = 0x21 (class, interface, host-to-device)
	 *   bRequest = HID_SET_REPORT
	 *   wValue = (HID_REPORT_TYPE_OUTPUT << 8) | 0
	 *   wIndex = interface number
	 *   wLength = 1
	 *   data = led byte
	 */
}

static void
usb_hid_alarm(clock_t stamp)
{
	expire_timers(stamp);
}

static void
usb_hid_other(message *m, int ipc_status)
{
	/* Route USB messages to the USB driver handler */
	if (!is_ipc_notify(ipc_status)) {
		usb_handle_msg(&usb_hid_driver, m);
	}
}

static struct inputdriver usb_hid_input = {
	.idr_leds   = usb_hid_set_leds,
	.idr_intr   = NULL,            /* No direct HW interrupts */
	.idr_alarm  = usb_hid_alarm,
	.idr_other  = usb_hid_other,
};


/*===========================================================================*
 *    Initialization                                                         *
 *===========================================================================*/
static int
usb_hid_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	init_timer(&poll_timer);
	hid_kbd_init();

	/* Register with usb_core as a class driver */
	if (usb_init("usb_hid") != 0) {
		printf("usb_hid: failed to register with usb_core\n");
		return ENODEV;
	}

	/* Announce ourselves as a keyboard to the input server */
	inputdriver_announce(INPUT_DEV_KBD);

	printf("usb_hid: initialized\n");
	return OK;
}

static void
usb_hid_startup(void)
{
	sef_setcb_init_fresh(usb_hid_init);
	sef_startup();
}


/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
int
main(void)
{
	usb_hid_startup();

	/* Enter the inputdriver main loop.
	 * USB messages arrive via the idr_other callback. */
	inputdriver_task(&usb_hid_input);

	return 0;
}
