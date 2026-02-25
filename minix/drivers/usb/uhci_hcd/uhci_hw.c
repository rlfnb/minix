/*
 * UHCI HCD - Hardware operations
 *
 * Implements UHCI hardware access: register I/O, TD/QH management,
 * frame list setup, and transfer execution. Derived from the original
 * usbd uhci_core.c with all DDEKit dependencies removed.
 *
 * Uses native MINIX system calls: sys_inw/sys_outw for I/O port access,
 * sys_umap for virtual-to-physical translation, alloc_contig for DMA memory.
 */

#include <string.h>
#include <minix/drivers.h>
#include <minix/syslib.h>

#include "uhci_hw.h"

/*===========================================================================*
 *    I/O port access                                                        *
 *===========================================================================*/

hcd_reg2
uhci_reg_read16(uhci_state *uhci, hcd_reg4 reg)
{
	u32_t value;
	if (sys_inw(uhci->io_base + reg, &value) != OK)
		return 0;
	return (hcd_reg2)(value & 0xFFFF);
}

void
uhci_reg_write16(uhci_state *uhci, hcd_reg4 reg, hcd_reg2 val)
{
	sys_outw(uhci->io_base + reg, val);
}

static hcd_reg4
uhci_reg_read32(uhci_state *uhci, hcd_reg4 reg)
{
	u32_t value;
	if (sys_inl(uhci->io_base + reg, &value) != OK)
		return 0;
	return (hcd_reg4)value;
}

static void
uhci_reg_write32(uhci_state *uhci, hcd_reg4 reg, hcd_reg4 val)
{
	sys_outl(uhci->io_base + reg, val);
}


/*===========================================================================*
 *    Physical address translation                                           *
 *===========================================================================*/
static hcd_reg4
virt_to_phys(void *virt)
{
	phys_bytes phys;
	if (sys_umap(SELF, VM_D, (vir_bytes)virt, sizeof(hcd_reg4), &phys)
	    != OK)
		return 0;
	return (hcd_reg4)phys;
}


/*===========================================================================*
 *    TD / QH pool management                                                *
 *===========================================================================*/
static uhci_td *
alloc_td(uhci_state *uhci)
{
	uhci_td *td;

	if (uhci->td_used >= UHCI_MAX_TDS) {
		printf("uhci_hcd: out of TDs\n");
		return NULL;
	}

	td = &uhci->td_pool[uhci->td_used];
	memset(td, 0, sizeof(*td));
	td->_phys = virt_to_phys(td);
	uhci->td_used++;
	return td;
}

static void
free_all_tds(uhci_state *uhci)
{
	uhci->td_used = 0;
}

static void
fill_td(uhci_td *td, hcd_reg4 link, hcd_reg4 ctrl,
	hcd_reg4 token, hcd_reg4 buffer)
{
	td->link_ptr = link;
	td->ctrl_sts = ctrl;
	td->token = token;
	td->buffer_ptr = buffer;
}

static uhci_qh *
alloc_qh(uhci_state *uhci)
{
	uhci_qh *qh;

	if (uhci->qh_used >= UHCI_MAX_QHS) {
		printf("uhci_hcd: out of QHs\n");
		return NULL;
	}

	qh = &uhci->qh_pool[uhci->qh_used];
	memset(qh, 0, sizeof(*qh));
	qh->_phys = virt_to_phys(qh);
	uhci->qh_used++;
	return qh;
}

static void
free_all_qhs(uhci_state *uhci)
{
	uhci->qh_used = 0;
}


/*===========================================================================*
 *    Transfer submission helpers                                            *
 *===========================================================================*/
static void
submit_td_chain(uhci_state *uhci, uhci_td *td_head)
{
	uhci_qh *qh;
	int i;

	qh = alloc_qh(uhci);
	if (qh == NULL)
		return;

	qh->head_lp = UHCI_QH_HLP_TERM;
	qh->elem_lp = td_head->_phys | UHCI_TD_LP_DEPTH;

	for (i = 0; i < UHCI_FRAMELIST_COUNT; i++)
		uhci->frame_list[i] = qh->_phys | UHCI_FLP_QH;
}

static int
wait_for_completion(uhci_state *uhci)
{
	uhci_td *td;
	hcd_reg2 sts;
	int timeout, i;

	for (timeout = 0; timeout < 5000; timeout++) {
		sts = uhci_reg_read16(uhci, UHCI_REG_USBSTS);

		if (sts & UHCI_STS_ALLINTRS)
			uhci_reg_write16(uhci, UHCI_REG_USBSTS,
				sts & UHCI_STS_ALLINTRS);

		if (sts & UHCI_STS_HSE)
			return -1;

		/* Check if all TDs are done */
		for (i = 0; i < uhci->td_used; i++) {
			td = &uhci->td_pool[i];
			if (td->ctrl_sts & UHCI_TD_CS_ACTIVE)
				break;
			if (td->ctrl_sts & UHCI_TD_CS_ANY_ERROR)
				return -1;
		}

		if (i == uhci->td_used)
			return 0;  /* All TDs completed */

		micro_delay(1000);  /* 1ms */
	}

	printf("uhci_hcd: transfer timeout\n");
	return -1;
}

static void
clear_frame_list(uhci_state *uhci)
{
	int i;
	for (i = 0; i < UHCI_FRAMELIST_COUNT; i++)
		uhci->frame_list[i] = UHCI_FLP_T;
}

/* Calculate actual bytes transferred from completed IN TDs */
static int
get_actual_length(uhci_state *uhci)
{
	int total = 0;
	int i;

	for (i = 0; i < uhci->td_used; i++) {
		uhci_td *td = &uhci->td_pool[i];
		if ((td->token & UHCI_TD_TOK_PID_MASK) == UHCI_TD_PID_IN) {
			int len = td->ctrl_sts & UHCI_TD_CS_ACTLEN_MASK;
			total += (len == 0x7FF) ? 0 : len + 1;
		}
	}

	return total;
}


/*===========================================================================*
 *    Controller initialization                                              *
 *===========================================================================*/

int
uhci_hw_init(uhci_state *uhci)
{
	int i;
	phys_bytes phys;

	uhci->td_used = 0;
	uhci->qh_used = 0;
	memset(uhci->td_pool, 0, sizeof(uhci->td_pool));
	memset(uhci->qh_pool, 0, sizeof(uhci->qh_pool));

	/* Allocate 4K-aligned frame list */
	uhci->frame_list = alloc_contig(
		UHCI_FRAMELIST_COUNT * sizeof(hcd_reg4),
		AC_ALIGN4K, &phys);

	if (uhci->frame_list == NULL) {
		printf("uhci_hcd: failed to allocate frame list\n");
		return -1;
	}
	uhci->frame_list_phys = (hcd_reg4)phys;

	for (i = 0; i < UHCI_FRAMELIST_COUNT; i++)
		uhci->frame_list[i] = UHCI_FLP_T;

	uhci->port_connected[0] = 0;
	uhci->port_connected[1] = 0;
	uhci->rx_len = 0;

	return 0;
}


void
uhci_hw_reset(uhci_state *uhci)
{
	hcd_reg2 cmd;
	int timeout;

	/* Global reset */
	uhci_reg_write16(uhci, UHCI_REG_USBCMD, UHCI_CMD_GRESET);
	micro_delay(50000);  /* 50ms */
	uhci_reg_write16(uhci, UHCI_REG_USBCMD, 0);
	micro_delay(10000);  /* 10ms */

	/* Host controller reset */
	uhci_reg_write16(uhci, UHCI_REG_USBCMD, UHCI_CMD_HCRESET);

	for (timeout = 0; timeout < 100; timeout++) {
		cmd = uhci_reg_read16(uhci, UHCI_REG_USBCMD);
		if (!(cmd & UHCI_CMD_HCRESET))
			break;
		micro_delay(1000);
	}

	if (cmd & UHCI_CMD_HCRESET)
		printf("uhci_hcd: reset did not complete\n");

	/* Clear status and frame number */
	uhci_reg_write16(uhci, UHCI_REG_USBSTS, UHCI_STS_ALLINTRS);
	uhci_reg_write16(uhci, UHCI_REG_FRNUM, 0);

	/* Set SOF timing */
	{
		u32_t val = 0x40;
		sys_outb(uhci->io_base + UHCI_REG_SOFMOD, val);
	}
}


void
uhci_hw_start(uhci_state *uhci)
{
	uhci_reg_write32(uhci, UHCI_REG_FLBASEADD, uhci->frame_list_phys);
	uhci_reg_write16(uhci, UHCI_REG_FRNUM, 0);
	uhci_reg_write16(uhci, UHCI_REG_USBINTR, UHCI_INTR_ALL);
	uhci_reg_write16(uhci, UHCI_REG_USBCMD,
		UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);

	printf("uhci_hcd: controller started (I/O base 0x%04X)\n",
		(unsigned)uhci->io_base);
}


void
uhci_hw_stop(uhci_state *uhci)
{
	hcd_reg2 sts;
	int timeout;

	uhci_reg_write16(uhci, UHCI_REG_USBINTR, 0);
	uhci_reg_write16(uhci, UHCI_REG_USBCMD, 0);

	for (timeout = 0; timeout < 100; timeout++) {
		sts = uhci_reg_read16(uhci, UHCI_REG_USBSTS);
		if (sts & UHCI_STS_HCH)
			break;
		micro_delay(1000);
	}

	uhci_reg_write16(uhci, UHCI_REG_USBSTS, UHCI_STS_ALLINTRS);
	printf("uhci_hcd: controller stopped\n");
}


/*===========================================================================*
 *    Port management                                                        *
 *===========================================================================*/

static hcd_reg4
portsc_reg(int port)
{
	return (port == 0) ? UHCI_REG_PORTSC0 : UHCI_REG_PORTSC1;
}


int
uhci_port_read_status(uhci_state *uhci, int port)
{
	return uhci_reg_read16(uhci, portsc_reg(port));
}


int
uhci_port_reset(uhci_state *uhci, int port, int *speed)
{
	hcd_reg2 portsc;
	int timeout;

	/* Issue port reset */
	portsc = uhci_reg_read16(uhci, portsc_reg(port));
	uhci_reg_write16(uhci, portsc_reg(port), portsc | UHCI_PORTSC_PR);

	/* Hold reset for 50ms */
	micro_delay(50000);

	/* Clear reset */
	portsc = uhci_reg_read16(uhci, portsc_reg(port));
	uhci_reg_write16(uhci, portsc_reg(port), portsc & ~UHCI_PORTSC_PR);
	micro_delay(10000);

	/* Enable port */
	portsc = uhci_reg_read16(uhci, portsc_reg(port));
	uhci_reg_write16(uhci, portsc_reg(port), portsc | UHCI_PORTSC_PE);

	/* Wait for port enable */
	for (timeout = 0; timeout < 100; timeout++) {
		portsc = uhci_reg_read16(uhci, portsc_reg(port));
		if (portsc & UHCI_PORTSC_PE)
			break;
		micro_delay(1000);
	}

	if (!(portsc & UHCI_PORTSC_PE)) {
		printf("uhci_hcd: failed to enable port %d\n", port);
		return -1;
	}

	/* Clear change bits */
	uhci_reg_write16(uhci, portsc_reg(port),
		portsc | UHCI_PORTSC_WC_BITS);

	/* Determine speed */
	portsc = uhci_reg_read16(uhci, portsc_reg(port));
	*speed = (portsc & UHCI_PORTSC_LSDA) ?
		UHCI_SPEED_LOW : UHCI_SPEED_FULL;

	micro_delay(10000);  /* Let device settle */
	return 0;
}


/*===========================================================================*
 *    Control transfer                                                       *
 *===========================================================================*/
int
uhci_control_transfer(uhci_state *uhci, int addr, int ep,
	usb_device_request_t *setup, hcd_reg1 *data, int data_len,
	int direction, int speed)
{
	uhci_td *td, *prev_td;
	hcd_reg4 token, ctrl;
	int toggle, remaining, pkt_size, actual;

	free_all_tds(uhci);
	free_all_qhs(uhci);

	/* -- SETUP stage -- */
	td = alloc_td(uhci);
	if (td == NULL)
		return -1;

	token = UHCI_TD_PID_SETUP |
		((hcd_reg4)addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_MAXLEN_ENCODE(sizeof(*setup));

	ctrl = UHCI_TD_CS_ACTIVE | (3u << UHCI_TD_CS_ERRCNT_SHIFT);
	if (speed == UHCI_SPEED_LOW)
		ctrl |= UHCI_TD_CS_LS;

	fill_td(td, UHCI_TD_LP_TERM, ctrl, token, virt_to_phys(setup));
	submit_td_chain(uhci, td);

	if (wait_for_completion(uhci) != 0) {
		clear_frame_list(uhci);
		return -1;
	}
	clear_frame_list(uhci);

	/* -- DATA stage (if any) -- */
	if (data_len > 0 && data != NULL) {
		free_all_tds(uhci);
		free_all_qhs(uhci);

		remaining = data_len;
		prev_td = NULL;
		toggle = DATATOG_DATA1;  /* First data packet is DATA1 */
		pkt_size = (speed == UHCI_SPEED_LOW) ?
			UHCI_MAX_PACKET_LS : UHCI_MAX_PACKET_FS;

		while (remaining > 0) {
			int chunk = (remaining > pkt_size) ?
				pkt_size : remaining;

			td = alloc_td(uhci);
			if (td == NULL) {
				clear_frame_list(uhci);
				return -1;
			}

			token = (direction ? UHCI_TD_PID_IN :
				UHCI_TD_PID_OUT) |
				((hcd_reg4)addr <<
					UHCI_TD_TOK_DEVADDR_SHIFT) |
				((hcd_reg4)ep <<
					UHCI_TD_TOK_ENDPT_SHIFT) |
				UHCI_TD_MAXLEN_ENCODE(chunk);

			if (toggle)
				token |= UHCI_TD_TOK_DATATOG;

			ctrl = UHCI_TD_CS_ACTIVE |
				(3u << UHCI_TD_CS_ERRCNT_SHIFT);
			if (speed == UHCI_SPEED_LOW)
				ctrl |= UHCI_TD_CS_LS;
			if (direction)
				ctrl |= UHCI_TD_CS_SPD;

			fill_td(td, UHCI_TD_LP_TERM, ctrl, token,
				virt_to_phys(data + (data_len - remaining)));

			if (prev_td != NULL)
				prev_td->link_ptr = td->_phys |
					UHCI_TD_LP_DEPTH;

			prev_td = td;
			remaining -= chunk;
			toggle ^= 1;
		}

		if (prev_td != NULL)
			prev_td->ctrl_sts |= UHCI_TD_CS_IOC;

		submit_td_chain(uhci, &uhci->td_pool[0]);

		if (wait_for_completion(uhci) != 0) {
			clear_frame_list(uhci);
			return -1;
		}

		actual = get_actual_length(uhci);
		clear_frame_list(uhci);
	} else {
		actual = 0;
	}

	/* -- STATUS stage -- */
	free_all_tds(uhci);
	free_all_qhs(uhci);

	td = alloc_td(uhci);
	if (td == NULL)
		return -1;

	/* Status is opposite direction of data, or IN if no data */
	token = ((data_len > 0 && !direction) ?
		UHCI_TD_PID_IN : UHCI_TD_PID_OUT) |
		((hcd_reg4)addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_TOK_DATATOG |
		UHCI_TD_MAXLEN_ENCODE(0);

	ctrl = UHCI_TD_CS_ACTIVE | (3u << UHCI_TD_CS_ERRCNT_SHIFT) |
		UHCI_TD_CS_IOC;
	if (speed == UHCI_SPEED_LOW)
		ctrl |= UHCI_TD_CS_LS;

	fill_td(td, UHCI_TD_LP_TERM, ctrl, token, 0);
	submit_td_chain(uhci, td);

	if (wait_for_completion(uhci) != 0) {
		clear_frame_list(uhci);
		return -1;
	}
	clear_frame_list(uhci);

	return actual;
}


/*===========================================================================*
 *    Interrupt transfer                                                     *
 *===========================================================================*/
int
uhci_interrupt_transfer(uhci_state *uhci, int addr, int ep,
	hcd_reg1 *data, int data_len, int direction, int speed,
	int max_packet_size)
{
	uhci_td *td;
	hcd_reg4 token, ctrl;
	int actual;

	free_all_tds(uhci);
	free_all_qhs(uhci);

	td = alloc_td(uhci);
	if (td == NULL)
		return -1;

	token = (direction ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT) |
		((hcd_reg4)addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_MAXLEN_ENCODE(data_len);

	/* Use DATA0 for first interrupt transfer.
	 * Toggle tracking would be needed for continuous polling. */
	if (uhci->cur_datatog)
		token |= UHCI_TD_TOK_DATATOG;

	ctrl = UHCI_TD_CS_ACTIVE | (3u << UHCI_TD_CS_ERRCNT_SHIFT) |
		UHCI_TD_CS_IOC | UHCI_TD_CS_SPD;
	if (speed == UHCI_SPEED_LOW)
		ctrl |= UHCI_TD_CS_LS;

	fill_td(td, UHCI_TD_LP_TERM, ctrl, token, virt_to_phys(data));
	submit_td_chain(uhci, td);

	if (wait_for_completion(uhci) != 0) {
		clear_frame_list(uhci);
		return -1;
	}

	actual = get_actual_length(uhci);
	clear_frame_list(uhci);

	/* Toggle for next transfer */
	uhci->cur_datatog ^= 1;

	return actual;
}
