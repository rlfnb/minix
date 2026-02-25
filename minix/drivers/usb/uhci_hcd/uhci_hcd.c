/*
 * UHCI Host Controller Driver - Standalone MINIX process
 *
 * This is a native MINIX USB HCD driver for Intel UHCI controllers.
 * It runs as a standalone process with a single sef_receive_status()
 * message loop - no DDEKit, no user-space threading.
 *
 * Responsibilities:
 * - PCI device detection and initialization
 * - Native MINIX IRQ handling
 * - Registration with usb_core via IPC
 * - Executing USB transfers (URBs) from usb_core
 * - Port polling and status reporting
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/usb.h>
#include <minix/usb_hcd.h>
#include <minix/safecopies.h>
#include <minix/syslib.h>
#include <machine/pci.h>
#include <string.h>

#include "uhci_hw.h"

/* Global state */
static uhci_state uhci;
static endpoint_t core_ep = NONE;
static int running = 1;
static minix_timer_t port_poll_timer;


/*===========================================================================*
 *    PCI detection and initialization                                       *
 *===========================================================================*/
static int
uhci_pci_init(void)
{
	int devind, r;
	u16_t vid, did, cr;
	u8_t base_class, sub_class, prog_if;
	u32_t io_base, io_size;
	int io_flag;

	memset(&uhci, 0, sizeof(uhci));

	pci_init();

	/* Scan for UHCI controller */
	r = pci_first_dev(&devind, &vid, &did);
	while (r == 1) {
		base_class = pci_attr_r8(devind, PCI_BCR);
		sub_class = pci_attr_r8(devind, PCI_SCR);
		prog_if = pci_attr_r8(devind, PCI_PIFR);

		if (base_class == UHCI_PCI_CLASS &&
		    sub_class == UHCI_PCI_SUBCLASS &&
		    prog_if == UHCI_PCI_INTERFACE) {
			printf("uhci_hcd: found UHCI controller "
				"(vid=0x%04X did=0x%04X)\n", vid, did);
			uhci.devind = devind;
			goto found;
		}

		r = pci_next_dev(&devind, &vid, &did);
	}

	printf("uhci_hcd: no UHCI controller found\n");
	return -1;

found:
	pci_reserve(devind);

	/* Get I/O BAR (UHCI uses BAR4 = PCI_BAR_5, or BAR0 as fallback) */
	r = pci_get_bar(devind, PCI_BAR_5, &io_base, &io_size, &io_flag);
	if (r != OK || !(io_flag & PCI_BAR_IO)) {
		r = pci_get_bar(devind, PCI_BAR, &io_base, &io_size, &io_flag);
		if (r != OK || !(io_flag & PCI_BAR_IO)) {
			printf("uhci_hcd: no I/O BAR found\n");
			return -1;
		}
	}

	uhci.io_base = io_base;
	printf("uhci_hcd: I/O base=0x%04X size=%u\n",
		(unsigned)io_base, (unsigned)io_size);

	/* Get IRQ */
	uhci.irq = pci_attr_r8(devind, PCI_ILR);
	printf("uhci_hcd: IRQ=%d\n", uhci.irq);

	/* Enable bus mastering and I/O space */
	cr = pci_attr_r16(devind, PCI_CR);
	if (!(cr & PCI_CR_MAST_EN))
		pci_attr_w16(devind, PCI_CR, cr | PCI_CR_MAST_EN);
	cr = pci_attr_r16(devind, PCI_CR);
	if (!(cr & PCI_CR_IO_EN))
		pci_attr_w16(devind, PCI_CR, cr | PCI_CR_IO_EN);

	/* Request I/O privilege */
	{
		struct minix_mem_range mr;
		mr.mr_base = io_base;
		mr.mr_limit = io_base + io_size;
		if (sys_privctl(SELF, SYS_PRIV_ADD_IO, &mr) != OK) {
			printf("uhci_hcd: I/O privilege failed\n");
			return -1;
		}
	}

	/* Disable BIOS legacy support */
	pci_attr_w16(devind, UHCI_PCI_LEGSUP, UHCI_PCI_LEGSUP_DEFAULT);

	/* Initialize UHCI hardware */
	if (uhci_hw_init(&uhci) != 0) {
		printf("uhci_hcd: hardware init failed\n");
		return -1;
	}

	uhci_hw_reset(&uhci);

	/* Set up native MINIX IRQ handling */
	uhci.irq_hook_id = uhci.irq;
	if (sys_irqsetpolicy(uhci.irq, IRQ_REENABLE, &uhci.irq_hook_id)
	    != OK) {
		printf("uhci_hcd: IRQ policy failed\n");
		return -1;
	}
	if (sys_irqenable(&uhci.irq_hook_id) != OK) {
		printf("uhci_hcd: IRQ enable failed\n");
		return -1;
	}

	/* Start the controller */
	uhci_hw_start(&uhci);

	return 0;
}


/*===========================================================================*
 *    Registration with usb_core                                             *
 *===========================================================================*/
static int
register_with_core(void)
{
	message m;
	int r;

	/* Find usb_core via DS */
	r = ds_retrieve_label_endpt(USB_CORE_LABEL, &core_ep);
	if (r != OK) {
		printf("uhci_hcd: usb_core not found in DS (r=%d)\n", r);
		return -1;
	}

	printf("uhci_hcd: found usb_core at ep=%d\n", core_ep);

	/* Send registration message */
	memset(&m, 0, sizeof(m));
	m.m_type = USB_HCD_REGISTER;
	m.USB_HCD_PORTS = UHCI_NUM_PORTS;
	m.USB_HCD_CAPS = USB_HCD_CAP_LS | USB_HCD_CAP_FS;

	r = ipc_sendrec(core_ep, &m);
	if (r != OK) {
		printf("uhci_hcd: registration sendrec failed: %d\n", r);
		return -1;
	}

	if (m.m_type != USB_HCD_REGISTER_REPLY || m.USB_RESULT != OK) {
		printf("uhci_hcd: registration rejected\n");
		return -1;
	}

	printf("uhci_hcd: registered with usb_core (id=%ld)\n",
		m.USB_HCD_ID);
	return 0;
}


/*===========================================================================*
 *    Port polling                                                           *
 *===========================================================================*/
static void
poll_ports(void)
{
	int port, status, speed;
	message m;

	for (port = 0; port < UHCI_NUM_PORTS; port++) {
		status = uhci_port_read_status(&uhci, port);

		/* Check connect status change */
		if (!(status & UHCI_PORTSC_CSC))
			continue;

		/* Clear change bit */
		uhci_reg_write16(&uhci,
			(port == 0) ? UHCI_REG_PORTSC0 : UHCI_REG_PORTSC1,
			status | UHCI_PORTSC_CSC);

		if (status & UHCI_PORTSC_CCS) {
			/* Device connected */
			if (!uhci.port_connected[port]) {
				uhci.port_connected[port] = 1;
				printf("uhci_hcd: device connected "
					"on port %d\n", port);

				/* Reset the port and determine speed */
				if (uhci_port_reset(&uhci, port, &speed)
				    == 0) {
					/* Notify usb_core */
					if (core_ep != NONE) {
						memset(&m, 0, sizeof(m));
						m.m_type =
							USB_HCD_RESET_DONE;
						m.USB_HCD_PORT = port;
						m.USB_HCD_SPEED = speed;
						ipc_send(core_ep, &m);
					}
				}
			}
		} else {
			/* Device disconnected */
			if (uhci.port_connected[port]) {
				uhci.port_connected[port] = 0;
				printf("uhci_hcd: device disconnected "
					"from port %d\n", port);

				if (core_ep != NONE) {
					memset(&m, 0, sizeof(m));
					m.m_type = USB_HCD_PORT_STATUS;
					m.USB_HCD_PORT = port;
					m.USB_HCD_PSTATUS = 0;
					ipc_send(core_ep, &m);
				}
			}
		}
	}
}


/*===========================================================================*
 *    Interrupt handler                                                      *
 *===========================================================================*/
static void
uhci_handle_interrupt(void)
{
	hcd_reg2 status;

	status = uhci_reg_read16(&uhci, UHCI_REG_USBSTS);

	if (!(status & UHCI_STS_ALLINTRS)) {
		sys_irqenable(&uhci.irq_hook_id);
		return;
	}

	/* Acknowledge */
	uhci_reg_write16(&uhci, UHCI_REG_USBSTS,
		status & UHCI_STS_ALLINTRS);

	if (status & UHCI_STS_HSE)
		printf("uhci_hcd: host system error!\n");
	if (status & UHCI_STS_HCPE)
		printf("uhci_hcd: host controller process error!\n");

	/* Check ports for changes */
	poll_ports();

	sys_irqenable(&uhci.irq_hook_id);
}


/*===========================================================================*
 *    Handle URB submission from usb_core                                    *
 *===========================================================================*/
static void
handle_submit_urb(message *m)
{
	cp_grant_id_t grant = m->USB_GRANT_ID;
	size_t grant_size = m->USB_GRANT_SIZE;
	unsigned int urb_id = m->USB_URB_ID;
	struct usb_urb urb_buf;
	char data_buf[1024];
	message reply;
	int r, actual;

	/* Copy URB header from usb_core */
	if (grant_size > sizeof(data_buf))
		grant_size = sizeof(data_buf);

	r = sys_safecopyfrom(m->m_source, grant, 0,
		(vir_bytes)data_buf, grant_size);
	if (r != OK) {
		printf("uhci_hcd: safecopy URB failed: %d\n", r);
		return;
	}

	/* Parse the URB.
	 * The grant starts at dev_id (next pointer was excluded). */
	memcpy(&urb_buf.dev_id, data_buf, sizeof(data_buf));

	/* Execute the transfer based on type */
	switch (urb_buf.type) {
	case USB_TRANSFER_CTL:
		actual = uhci_control_transfer(&uhci,
			urb_buf.dev_id,  /* Using dev_id as address for now */
			urb_buf.endpoint,
			(usb_device_request_t *)urb_buf.setup_packet,
			(hcd_reg1 *)urb_buf.buffer,
			urb_buf.size,
			urb_buf.direction,
			UHCI_SPEED_FULL);

		urb_buf.status = (actual >= 0) ? 0 : -1;
		urb_buf.actual_length = (actual >= 0) ? actual : 0;
		break;

	case USB_TRANSFER_INT:
		actual = uhci_interrupt_transfer(&uhci,
			urb_buf.dev_id,
			urb_buf.endpoint,
			(hcd_reg1 *)urb_buf.buffer,
			urb_buf.size,
			urb_buf.direction,
			UHCI_SPEED_LOW,
			8);

		urb_buf.status = (actual >= 0) ? 0 : -1;
		urb_buf.actual_length = (actual >= 0) ? actual : 0;
		break;

	default:
		printf("uhci_hcd: unsupported transfer type %d\n",
			urb_buf.type);
		urb_buf.status = -1;
		urb_buf.actual_length = 0;
		break;
	}

	/* Copy result back to usb_core via grant */
	memcpy(data_buf, &urb_buf.dev_id,
		grant_size);
	sys_safecopyto(m->m_source, grant, 0,
		(vir_bytes)data_buf, grant_size);

	/* Notify usb_core of completion */
	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_HCD_URB_COMPLETE;
	reply.USB_URB_ID = urb_id;
	ipc_send(m->m_source, &reply);
}


/*===========================================================================*
 *    Handle port reset request from usb_core                                *
 *===========================================================================*/
static void
handle_reset_port(message *m)
{
	int port = m->USB_HCD_PORT;
	int speed;
	message reply;

	if (port < 0 || port >= UHCI_NUM_PORTS) {
		printf("uhci_hcd: invalid port %d\n", port);
		return;
	}

	uhci_port_reset(&uhci, port, &speed);

	memset(&reply, 0, sizeof(reply));
	reply.m_type = USB_HCD_RESET_DONE;
	reply.USB_HCD_PORT = port;
	reply.USB_HCD_SPEED = speed;
	ipc_send(m->m_source, &reply);
}


/*===========================================================================*
 *    Port poll timer                                                        *
 *===========================================================================*/
static void
port_poll_alarm(int UNUSED(arg))
{
	poll_ports();

	/* Reschedule: poll every second */
	set_timer(&port_poll_timer, sys_hz(), port_poll_alarm, 0);
}


/*===========================================================================*
 *    SEF init                                                               *
 *===========================================================================*/
static int
uhci_hcd_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	init_timer(&port_poll_timer);

	/* Initialize PCI and UHCI hardware */
	if (uhci_pci_init() != 0) {
		printf("uhci_hcd: initialization failed\n");
		return ENODEV;
	}

	/* Register with usb_core */
	if (register_with_core() != 0) {
		printf("uhci_hcd: failed to register with usb_core\n");
		/* Continue anyway - usb_core may not be running yet.
		 * We'll retry on timer or when usb_core contacts us. */
	}

	/* Start port polling timer */
	set_timer(&port_poll_timer, sys_hz(), port_poll_alarm, 0);

	/* Do initial port scan */
	poll_ports();

	printf("uhci_hcd: initialized\n");
	return OK;
}


/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
int
main(void)
{
	message m;
	int r, ipc_status;

	sef_setcb_init_fresh(uhci_hcd_init);
	sef_startup();

	while (running) {
		r = sef_receive_status(ANY, &m, &ipc_status);
		if (r != OK) {
			if (r == EINTR)
				continue;
			panic("uhci_hcd: receive failed: %d", r);
		}

		if (is_ipc_notify(ipc_status)) {
			switch (_ENDPOINT_P(m.m_source)) {
			case HARDWARE:
				uhci_handle_interrupt();
				break;
			case CLOCK:
				expire_timers(m.m_notify.timestamp);
				break;
			default:
				break;
			}
			continue;
		}

		switch (m.m_type) {
		case USB_HCD_SUBMIT_URB:
			handle_submit_urb(&m);
			break;
		case USB_HCD_RESET_PORT:
			handle_reset_port(&m);
			break;
		default:
			printf("uhci_hcd: unknown msg type %d from %d\n",
				m.m_type, m.m_source);
			break;
		}
	}

	uhci_hw_stop(&uhci);
	return 0;
}
