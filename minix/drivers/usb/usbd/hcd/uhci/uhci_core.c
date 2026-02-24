/*
 * Implementation of UHCI (Universal Host Controller Interface) core logic
 *
 * UHCI is a PCI-based USB 1.1 host controller designed by Intel.
 * Unlike MUSB (memory-mapped), UHCI uses I/O port access and a
 * frame list with Queue Heads (QH) and Transfer Descriptors (TD)
 * in main memory for USB transfer scheduling.
 *
 * This implementation provides the HCD interface functions required
 * by the generic USBD layer, enabling USB device enumeration and
 * data transfer over UHCI controllers.
 */

#include <string.h>				/* memcpy, memset */

#include <minix/drivers.h>			/* errno, micro_delay */
#include <minix/syslib.h>			/* sys_inb, sys_outb etc. */

#include <usbd/hcd_common.h>
#include <usbd/hcd_interface.h>
#include <usbd/usbd_common.h>

#include "uhci_core.h"
#include "uhci_regs.h"


/*===========================================================================*
 *    Local prototypes                                                       *
 *===========================================================================*/
/* I/O port access helpers */
static hcd_reg2 uhci_reg_read16(uhci_core_config *, hcd_reg4);
static void uhci_reg_write16(uhci_core_config *, hcd_reg4, hcd_reg2);
static hcd_reg4 uhci_reg_read32(uhci_core_config *, hcd_reg4);
static void uhci_reg_write32(uhci_core_config *, hcd_reg4, hcd_reg4);

/* TD management */
static uhci_td * uhci_alloc_td(uhci_core_config *);
static void uhci_free_all_tds(uhci_core_config *);
static void uhci_fill_td(uhci_td *, hcd_reg4, hcd_reg4, hcd_reg4, hcd_reg4);

/* QH management */
static uhci_qh * uhci_alloc_qh(uhci_core_config *);
static void uhci_free_all_qhs(uhci_core_config *);

/* Physical address helpers */
static hcd_reg4 uhci_virt_to_phys(void *);

/* Transfer helpers */
static void uhci_submit_td_chain(uhci_core_config *, uhci_td *);
static int uhci_wait_for_completion(uhci_core_config *);

/* Port SC register address for a given port */
static hcd_reg4 uhci_portsc_reg(int);

/* Internal state */
static uhci_td * current_td_head;	/* Head of current TD chain */
static int current_rx_len;		/* Bytes received in last transfer */
static hcd_reg1 current_rx_buf[MAX_WTOTALLENGTH]; /* Receive buffer */


/*===========================================================================*
 *    I/O port access                                                        *
 *===========================================================================*/

/*===========================================================================*
 *    uhci_reg_read16                                                        *
 *===========================================================================*/
static hcd_reg2
uhci_reg_read16(uhci_core_config * cfg, hcd_reg4 reg)
{
	u32_t value;

	if (sys_inw(cfg->io_base + reg, &value) != OK) {
		USB_MSG("Failed to read I/O port 0x%lX",
			(unsigned long)(cfg->io_base + reg));
		return 0;
	}
	return (hcd_reg2)(value & 0xFFFF);
}


/*===========================================================================*
 *    uhci_reg_write16                                                       *
 *===========================================================================*/
static void
uhci_reg_write16(uhci_core_config * cfg, hcd_reg4 reg, hcd_reg2 val)
{
	if (sys_outw(cfg->io_base + reg, val) != OK) {
		USB_MSG("Failed to write I/O port 0x%lX",
			(unsigned long)(cfg->io_base + reg));
	}
}


/*===========================================================================*
 *    uhci_reg_read32                                                        *
 *===========================================================================*/
static hcd_reg4
uhci_reg_read32(uhci_core_config * cfg, hcd_reg4 reg)
{
	u32_t value;

	if (sys_inl(cfg->io_base + reg, &value) != OK) {
		USB_MSG("Failed to read I/O port 0x%lX",
			(unsigned long)(cfg->io_base + reg));
		return 0;
	}
	return (hcd_reg4)value;
}


/*===========================================================================*
 *    uhci_reg_write32                                                       *
 *===========================================================================*/
static void
uhci_reg_write32(uhci_core_config * cfg, hcd_reg4 reg, hcd_reg4 val)
{
	if (sys_outl(cfg->io_base + reg, val) != OK) {
		USB_MSG("Failed to write I/O port 0x%lX",
			(unsigned long)(cfg->io_base + reg));
	}
}


/*===========================================================================*
 *    TD / QH management                                                     *
 *===========================================================================*/

/*===========================================================================*
 *    uhci_virt_to_phys                                                      *
 *===========================================================================*/
static hcd_reg4
uhci_virt_to_phys(void * virt)
{
	phys_bytes phys;

	if (sys_umap(SELF, VM_D, (vir_bytes)virt, sizeof(hcd_reg4), &phys)
		!= OK) {
		USB_MSG("Failed virt_to_phys translation");
		return 0;
	}
	return (hcd_reg4)phys;
}


/*===========================================================================*
 *    uhci_alloc_td                                                          *
 *===========================================================================*/
static uhci_td *
uhci_alloc_td(uhci_core_config * cfg)
{
	uhci_td * td;

	USB_ASSERT(cfg->td_used < UHCI_MAX_TDS, "Out of TDs");

	td = &(cfg->td_pool[cfg->td_used]);
	memset(td, 0, sizeof(*td));
	td->_phys = uhci_virt_to_phys(td);
	cfg->td_used++;

	return td;
}


/*===========================================================================*
 *    uhci_free_all_tds                                                      *
 *===========================================================================*/
static void
uhci_free_all_tds(uhci_core_config * cfg)
{
	cfg->td_used = 0;
}


/*===========================================================================*
 *    uhci_fill_td                                                           *
 *===========================================================================*/
static void
uhci_fill_td(uhci_td * td, hcd_reg4 link, hcd_reg4 ctrl_sts,
	hcd_reg4 token, hcd_reg4 buffer)
{
	td->link_ptr = link;
	td->ctrl_sts = ctrl_sts;
	td->token = token;
	td->buffer_ptr = buffer;
}


/*===========================================================================*
 *    uhci_alloc_qh                                                          *
 *===========================================================================*/
static uhci_qh *
uhci_alloc_qh(uhci_core_config * cfg)
{
	uhci_qh * qh;

	USB_ASSERT(cfg->qh_used < UHCI_MAX_QHS, "Out of QHs");

	qh = &(cfg->qh_pool[cfg->qh_used]);
	memset(qh, 0, sizeof(*qh));
	qh->_phys = uhci_virt_to_phys(qh);
	cfg->qh_used++;

	return qh;
}


/*===========================================================================*
 *    uhci_free_all_qhs                                                      *
 *===========================================================================*/
static void
uhci_free_all_qhs(uhci_core_config * cfg)
{
	cfg->qh_used = 0;
}


/*===========================================================================*
 *    uhci_portsc_reg                                                        *
 *===========================================================================*/
static hcd_reg4
uhci_portsc_reg(int port)
{
	return (0 == port) ? UHCI_REG_PORTSC0 : UHCI_REG_PORTSC1;
}


/*===========================================================================*
 *    Transfer submission and completion                                     *
 *===========================================================================*/

/*===========================================================================*
 *    uhci_submit_td_chain                                                   *
 *===========================================================================*/
static void
uhci_submit_td_chain(uhci_core_config * cfg, uhci_td * td_head)
{
	uhci_qh * qh;
	int i;

	/* Save for later inspection */
	current_td_head = td_head;

	/* Allocate a QH to wrap the TD chain */
	qh = uhci_alloc_qh(cfg);

	/* QH horizontal pointer: terminate (no next QH) */
	qh->head_lp = UHCI_QH_HLP_TERM;

	/* QH vertical pointer: first TD in chain */
	qh->elem_lp = td_head->_phys | UHCI_TD_LP_DEPTH;

	/* Insert QH into all frame list entries so it gets polled */
	for (i = 0; i < UHCI_FRAMELIST_COUNT; i++) {
		cfg->frame_list[i] = qh->_phys | UHCI_FLP_QH;
	}
}


/*===========================================================================*
 *    uhci_wait_for_completion                                               *
 *===========================================================================*/
static int
uhci_wait_for_completion(uhci_core_config * cfg)
{
	uhci_td * td;
	hcd_reg2 sts;
	int timeout;

	/* Poll-based wait for transfer completion:
	 * We check TDs and status register for up to 5 seconds */
	for (timeout = 0; timeout < 5000; timeout++) {

		/* Check status register for interrupts */
		sts = uhci_reg_read16(cfg, UHCI_REG_USBSTS);

		/* Acknowledge any pending status bits */
		if (sts & UHCI_STS_ALLINTRS)
			uhci_reg_write16(cfg, UHCI_REG_USBSTS,
				sts & UHCI_STS_ALLINTRS);

		/* Host system error is fatal */
		if (sts & UHCI_STS_HSE) {
			USB_MSG("UHCI Host System Error!");
			return EXIT_FAILURE;
		}

		/* Check if all TDs in the chain are done */
		td = current_td_head;
		while (NULL != td) {
			/* If any TD is still active, keep waiting */
			if (td->ctrl_sts & UHCI_TD_CS_ACTIVE)
				break;

			/* If this TD has an error, fail */
			if (td->ctrl_sts & UHCI_TD_CS_ANY_ERROR) {
				USB_MSG("UHCI TD error: 0x%08X",
					(unsigned int)td->ctrl_sts);
				return EXIT_FAILURE;
			}

			/* Move to next TD in chain */
			if (td->link_ptr & UHCI_TD_LP_TERM) {
				/* Last TD, all done */
				return EXIT_SUCCESS;
			}

			/* Walk to next TD */
			td = (uhci_td *)((hcd_addr)
				(td->link_ptr & ~0x0Fu));
			/* If we can't follow the pointer,
			 * use pool-based iteration */
			if (NULL == td)
				break;
		}

		/* If td is NULL, all TDs were checked and none active */
		if (NULL == td)
			return EXIT_SUCCESS;

		/* Wait 1ms before next poll */
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	USB_MSG("UHCI transfer timeout");
	return EXIT_FAILURE;
}


/*===========================================================================*
 *                                                                           *
 *    UHCI core implementation                                               *
 *                                                                           *
 *===========================================================================*/

/*===========================================================================*
 *    uhci_core_init                                                         *
 *===========================================================================*/
int
uhci_core_init(uhci_core_config * cfg)
{
	int i;

	DEBUG_DUMP;

	/* Initialize TD and QH pools */
	cfg->td_used = 0;
	cfg->qh_used = 0;
	memset(cfg->td_pool, 0, sizeof(cfg->td_pool));
	memset(cfg->qh_pool, 0, sizeof(cfg->qh_pool));

	/* Allocate frame list (must be 4K-aligned, 4K in size) */
	cfg->frame_list = alloc_contig(
		UHCI_FRAMELIST_COUNT * sizeof(hcd_reg4),
		AC_ALIGN4K, &(cfg->frame_list_phys));

	if (NULL == cfg->frame_list) {
		USB_MSG("Failed to allocate UHCI frame list");
		return EXIT_FAILURE;
	}

	/* Initialize all frame list entries as terminated */
	for (i = 0; i < UHCI_FRAMELIST_COUNT; i++) {
		cfg->frame_list[i] = UHCI_FLP_T;
	}

	/* Port connection tracking */
	cfg->port_connected[0] = 0;
	cfg->port_connected[1] = 0;

	/* Initialize static state */
	current_td_head = NULL;
	current_rx_len = 0;

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    uhci_core_reset                                                        *
 *===========================================================================*/
void
uhci_core_reset(uhci_core_config * cfg)
{
	hcd_reg2 cmd;

	DEBUG_DUMP;

	/* Issue Global Reset: resets all USB devices on the bus */
	uhci_reg_write16(cfg, UHCI_REG_USBCMD, UHCI_CMD_GRESET);
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(50));
	uhci_reg_write16(cfg, UHCI_REG_USBCMD, 0);
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(10));

	/* Issue Host Controller Reset */
	uhci_reg_write16(cfg, UHCI_REG_USBCMD, UHCI_CMD_HCRESET);

	/* Wait until reset completes (bit auto-clears) */
	{
		int timeout;
		for (timeout = 0; timeout < 100; timeout++) {
			cmd = uhci_reg_read16(cfg, UHCI_REG_USBCMD);
			if (!(cmd & UHCI_CMD_HCRESET))
				break;
			hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
		}

		if (cmd & UHCI_CMD_HCRESET)
			USB_MSG("UHCI reset did not complete!");
	}

	/* Clear status register */
	uhci_reg_write16(cfg, UHCI_REG_USBSTS, UHCI_STS_ALLINTRS);

	/* Clear frame number */
	uhci_reg_write16(cfg, UHCI_REG_FRNUM, 0);

	/* Set SOF timing to default */
	/* sys_outb for 8-bit register */
	{
		u32_t val = 0x40;  /* Default SOF timing value */
		sys_outb(cfg->io_base + UHCI_REG_SOFMOD, val);
	}
}


/*===========================================================================*
 *    uhci_core_start                                                        *
 *===========================================================================*/
void
uhci_core_start(uhci_core_config * cfg)
{
	DEBUG_DUMP;

	/* Set the frame list base address */
	uhci_reg_write32(cfg, UHCI_REG_FLBASEADD, cfg->frame_list_phys);

	/* Reset frame number */
	uhci_reg_write16(cfg, UHCI_REG_FRNUM, 0);

	/* Enable all interrupts */
	uhci_reg_write16(cfg, UHCI_REG_USBINTR, UHCI_INTR_ALL);

	/* Start the controller: Run, Configure Flag, Max Packet 64 */
	uhci_reg_write16(cfg, UHCI_REG_USBCMD,
		UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);

	USB_MSG("UHCI controller started (I/O base: 0x%04X)",
		(unsigned int)cfg->io_base);
}


/*===========================================================================*
 *    uhci_core_stop                                                         *
 *===========================================================================*/
void
uhci_core_stop(uhci_core_config * cfg)
{
	hcd_reg2 sts;
	int timeout;

	DEBUG_DUMP;

	/* Disable all interrupts */
	uhci_reg_write16(cfg, UHCI_REG_USBINTR, 0);

	/* Stop the controller */
	uhci_reg_write16(cfg, UHCI_REG_USBCMD, 0);

	/* Wait for HC to halt */
	for (timeout = 0; timeout < 100; timeout++) {
		sts = uhci_reg_read16(cfg, UHCI_REG_USBSTS);
		if (sts & UHCI_STS_HCH)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	/* Clear status */
	uhci_reg_write16(cfg, UHCI_REG_USBSTS, UHCI_STS_ALLINTRS);

	USB_MSG("UHCI controller stopped");
}


/*===========================================================================*
 *                                                                           *
 *    HCD interface implementation                                           *
 *                                                                           *
 *===========================================================================*/

/*===========================================================================*
 *    uhci_setup_device                                                      *
 *===========================================================================*/
void
uhci_setup_device(void * cfg, hcd_reg1 ep, hcd_reg1 addr,
		hcd_datatog * tx_tog, hcd_datatog * rx_tog)
{
	uhci_core_config * core;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;
	core->ep = ep;
	core->addr = addr;
	core->datatog_tx = tx_tog;
	core->datatog_rx = rx_tog;
}


/*===========================================================================*
 *    uhci_reset_device                                                      *
 *===========================================================================*/
int
uhci_reset_device(void * cfg, hcd_speed * speed)
{
	uhci_core_config * core;
	hcd_reg2 portsc;
	int port;
	int timeout;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	/* Set initial parameters */
	uhci_setup_device(core, HCD_DEFAULT_EP, HCD_DEFAULT_ADDR, NULL, NULL);

	/* Find which port has a device connected */
	port = -1;
	portsc = uhci_reg_read16(core, UHCI_REG_PORTSC0);
	if (portsc & UHCI_PORTSC_CCS) {
		port = 0;
	} else {
		portsc = uhci_reg_read16(core, UHCI_REG_PORTSC1);
		if (portsc & UHCI_PORTSC_CCS)
			port = 1;
	}

	if (port < 0) {
		USB_MSG("No device connected on any port");
		return EXIT_FAILURE;
	}

	/* Issue port reset */
	portsc = uhci_reg_read16(core, uhci_portsc_reg(port));
	uhci_reg_write16(core, uhci_portsc_reg(port),
		portsc | UHCI_PORTSC_PR);

	/* Hold reset for at least 50ms (USB spec) */
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(50));

	/* Clear reset */
	portsc = uhci_reg_read16(core, uhci_portsc_reg(port));
	uhci_reg_write16(core, uhci_portsc_reg(port),
		portsc & ~UHCI_PORTSC_PR);

	/* Wait a bit for device to recover */
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(10));

	/* Enable port */
	portsc = uhci_reg_read16(core, uhci_portsc_reg(port));
	uhci_reg_write16(core, uhci_portsc_reg(port),
		portsc | UHCI_PORTSC_PE);

	/* Wait for port enable */
	for (timeout = 0; timeout < 100; timeout++) {
		portsc = uhci_reg_read16(core, uhci_portsc_reg(port));
		if (portsc & UHCI_PORTSC_PE)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	if (!(portsc & UHCI_PORTSC_PE)) {
		USB_MSG("Failed to enable port %d", port);
		return EXIT_FAILURE;
	}

	/* Clear any change bits */
	uhci_reg_write16(core, uhci_portsc_reg(port),
		portsc | UHCI_PORTSC_WC_BITS);

	/* Determine speed: UHCI supports Low-Speed and Full-Speed */
	portsc = uhci_reg_read16(core, uhci_portsc_reg(port));
	if (portsc & UHCI_PORTSC_LSDA) {
		*speed = HCD_SPEED_LOW;
		USB_DBG("Low speed device on port %d", port);
	} else {
		*speed = HCD_SPEED_FULL;
		USB_DBG("Full speed device on port %d", port);
	}

	/* Allow device to settle after reset */
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(10));

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    uhci_setup_stage                                                       *
 *===========================================================================*/
void
uhci_setup_stage(void * cfg, hcd_ctrlrequest * setup)
{
	uhci_core_config * core;
	uhci_td * td;
	hcd_reg4 token;
	hcd_reg4 ctrl;
	hcd_reg4 buf_phys;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	USB_ASSERT(0 == core->ep, "Only EP 0 can handle control transfers");

	/* Free any leftover TDs/QHs from previous transfer */
	uhci_free_all_tds(core);
	uhci_free_all_qhs(core);

	/* Reset receive state */
	current_rx_len = 0;

	/* Allocate TD for SETUP packet */
	td = uhci_alloc_td(core);

	/* Build token for SETUP:
	 * PID=SETUP, device address, endpoint, data toggle=0, maxlen=8 */
	token = UHCI_TD_PID_SETUP |
		((hcd_reg4)core->addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)core->ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_MAXLEN_ENCODE(sizeof(*setup));
	/* SETUP always uses DATA0 */

	/* Control/status: active, 3 error retries,
	 * low speed if applicable */
	ctrl = UHCI_TD_CS_ACTIVE |
		(3u << UHCI_TD_CS_ERRCNT_SHIFT);

	/* Get physical address of setup packet data */
	buf_phys = uhci_virt_to_phys(setup);

	/* Terminate link (single TD for setup) */
	uhci_fill_td(td, UHCI_TD_LP_TERM, ctrl, token, buf_phys);

	/* Submit for execution */
	uhci_submit_td_chain(core, td);
}


/*===========================================================================*
 *    uhci_rx_stage                                                          *
 *===========================================================================*/
void
uhci_rx_stage(void * cfg, hcd_datarequest * request)
{
	uhci_core_config * core;
	uhci_td * td;
	uhci_td * prev_td;
	hcd_reg4 token;
	hcd_reg4 ctrl;
	hcd_reg4 buf_phys;
	int remaining;
	int pkt_size;
	hcd_datatog toggle;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	/* Free previous transfer's resources */
	uhci_free_all_tds(core);
	uhci_free_all_qhs(core);
	current_rx_len = 0;

	remaining = request->data_left;
	prev_td = NULL;
	toggle = (NULL != core->datatog_rx) ?
		*(core->datatog_rx) : HCD_DATATOG_DATA0;

	while (remaining > 0) {
		pkt_size = (remaining > (int)request->max_packet_size) ?
			(int)request->max_packet_size : remaining;

		td = uhci_alloc_td(core);

		/* Build token for IN */
		token = UHCI_TD_PID_IN |
			((hcd_reg4)core->addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
			((hcd_reg4)request->endpoint <<
				UHCI_TD_TOK_ENDPT_SHIFT) |
			UHCI_TD_MAXLEN_ENCODE(pkt_size);

		/* Apply data toggle */
		if (HCD_DATATOG_DATA1 == toggle)
			token |= UHCI_TD_TOK_DATATOG;

		/* Control: active, error count 3, short packet detect */
		ctrl = UHCI_TD_CS_ACTIVE |
			(3u << UHCI_TD_CS_ERRCNT_SHIFT) |
			UHCI_TD_CS_SPD;

		if (HCD_SPEED_LOW == request->speed)
			ctrl |= UHCI_TD_CS_LS;

		buf_phys = uhci_virt_to_phys(request->data +
			(request->data_left - remaining));

		uhci_fill_td(td, UHCI_TD_LP_TERM, ctrl, token, buf_phys);

		/* Chain to previous TD */
		if (NULL != prev_td)
			prev_td->link_ptr = td->_phys | UHCI_TD_LP_DEPTH;

		prev_td = td;
		remaining -= pkt_size;

		/* Toggle data toggle */
		toggle ^= HCD_DATATOG_DATA1;
	}

	/* Set IOC on last TD */
	if (NULL != prev_td)
		prev_td->ctrl_sts |= UHCI_TD_CS_IOC;

	/* Submit */
	uhci_submit_td_chain(core, &(core->td_pool[0]));
}


/*===========================================================================*
 *    uhci_tx_stage                                                          *
 *===========================================================================*/
void
uhci_tx_stage(void * cfg, hcd_datarequest * request)
{
	uhci_core_config * core;
	uhci_td * td;
	uhci_td * prev_td;
	hcd_reg4 token;
	hcd_reg4 ctrl;
	hcd_reg4 buf_phys;
	int remaining;
	int pkt_size;
	hcd_datatog toggle;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	/* Free previous transfer's resources */
	uhci_free_all_tds(core);
	uhci_free_all_qhs(core);

	remaining = request->data_left;
	prev_td = NULL;
	toggle = (NULL != core->datatog_tx) ?
		*(core->datatog_tx) : HCD_DATATOG_DATA0;

	while (remaining > 0) {
		pkt_size = (remaining > (int)request->max_packet_size) ?
			(int)request->max_packet_size : remaining;

		td = uhci_alloc_td(core);

		/* Build token for OUT */
		token = UHCI_TD_PID_OUT |
			((hcd_reg4)core->addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
			((hcd_reg4)request->endpoint <<
				UHCI_TD_TOK_ENDPT_SHIFT) |
			UHCI_TD_MAXLEN_ENCODE(pkt_size);

		/* Apply data toggle */
		if (HCD_DATATOG_DATA1 == toggle)
			token |= UHCI_TD_TOK_DATATOG;

		/* Control: active, error count 3 */
		ctrl = UHCI_TD_CS_ACTIVE |
			(3u << UHCI_TD_CS_ERRCNT_SHIFT);

		if (HCD_SPEED_LOW == request->speed)
			ctrl |= UHCI_TD_CS_LS;

		buf_phys = uhci_virt_to_phys(request->data +
			(request->data_left - remaining));

		uhci_fill_td(td, UHCI_TD_LP_TERM, ctrl, token, buf_phys);

		/* Chain to previous TD */
		if (NULL != prev_td)
			prev_td->link_ptr = td->_phys | UHCI_TD_LP_DEPTH;

		prev_td = td;
		remaining -= pkt_size;

		/* Toggle data toggle */
		toggle ^= HCD_DATATOG_DATA1;
	}

	/* Set IOC on last TD */
	if (NULL != prev_td)
		prev_td->ctrl_sts |= UHCI_TD_CS_IOC;

	/* Submit */
	uhci_submit_td_chain(core, &(core->td_pool[0]));
}


/*===========================================================================*
 *    uhci_in_data_stage                                                     *
 *===========================================================================*/
void
uhci_in_data_stage(void * cfg)
{
	uhci_core_config * core;
	uhci_td * td;
	hcd_reg4 token;
	hcd_reg4 ctrl;
	hcd_reg4 buf_phys;
	static hcd_datatog in_toggle = HCD_DATATOG_DATA1;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	/* Free previous and allocate new TD */
	uhci_free_all_tds(core);
	uhci_free_all_qhs(core);

	td = uhci_alloc_td(core);

	/* IN data with current toggle, max 64 bytes for control */
	token = UHCI_TD_PID_IN |
		((hcd_reg4)core->addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)core->ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_MAXLEN_ENCODE(64);

	if (HCD_DATATOG_DATA1 == in_toggle)
		token |= UHCI_TD_TOK_DATATOG;

	ctrl = UHCI_TD_CS_ACTIVE |
		(3u << UHCI_TD_CS_ERRCNT_SHIFT) |
		UHCI_TD_CS_IOC |
		UHCI_TD_CS_SPD;

	buf_phys = uhci_virt_to_phys(current_rx_buf + current_rx_len);

	uhci_fill_td(td, UHCI_TD_LP_TERM, ctrl, token, buf_phys);

	/* Toggle for next call */
	in_toggle ^= HCD_DATATOG_DATA1;

	/* Submit */
	uhci_submit_td_chain(core, td);
}


/*===========================================================================*
 *    uhci_out_data_stage                                                    *
 *===========================================================================*/
void
uhci_out_data_stage(void * cfg)
{
	DEBUG_DUMP;

	/* Set EP and device address to be used in this command */
	((void)cfg);

	USB_ASSERT(0, "Setup packet's 'DATA OUT' stage not implemented");
}


/*===========================================================================*
 *    uhci_in_status_stage                                                   *
 *===========================================================================*/
void
uhci_in_status_stage(void * cfg)
{
	uhci_core_config * core;
	uhci_td * td;
	hcd_reg4 token;
	hcd_reg4 ctrl;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	uhci_free_all_tds(core);
	uhci_free_all_qhs(core);

	td = uhci_alloc_td(core);

	/* IN status: zero-length IN with DATA1 */
	token = UHCI_TD_PID_IN |
		((hcd_reg4)core->addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)core->ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_TOK_DATATOG |		/* DATA1 for status */
		UHCI_TD_MAXLEN_ENCODE(0);	/* Zero-length */

	ctrl = UHCI_TD_CS_ACTIVE |
		(3u << UHCI_TD_CS_ERRCNT_SHIFT) |
		UHCI_TD_CS_IOC;

	uhci_fill_td(td, UHCI_TD_LP_TERM, ctrl, token, 0);

	uhci_submit_td_chain(core, td);
}


/*===========================================================================*
 *    uhci_out_status_stage                                                  *
 *===========================================================================*/
void
uhci_out_status_stage(void * cfg)
{
	uhci_core_config * core;
	uhci_td * td;
	hcd_reg4 token;
	hcd_reg4 ctrl;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	uhci_free_all_tds(core);
	uhci_free_all_qhs(core);

	td = uhci_alloc_td(core);

	/* OUT status: zero-length OUT with DATA1 */
	token = UHCI_TD_PID_OUT |
		((hcd_reg4)core->addr << UHCI_TD_TOK_DEVADDR_SHIFT) |
		((hcd_reg4)core->ep << UHCI_TD_TOK_ENDPT_SHIFT) |
		UHCI_TD_TOK_DATATOG |		/* DATA1 for status */
		UHCI_TD_MAXLEN_ENCODE(0);	/* Zero-length */

	ctrl = UHCI_TD_CS_ACTIVE |
		(3u << UHCI_TD_CS_ERRCNT_SHIFT) |
		UHCI_TD_CS_IOC;

	uhci_fill_td(td, UHCI_TD_LP_TERM, ctrl, token, 0);

	uhci_submit_td_chain(core, td);
}


/*===========================================================================*
 *    uhci_read_data                                                         *
 *===========================================================================*/
int
uhci_read_data(void * cfg, hcd_reg1 * buffer, hcd_reg1 ep_num)
{
	uhci_core_config * core;
	uhci_td * td;
	int actual_len;
	int i;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	/* Wait for the submitted TDs to complete */
	if (EXIT_SUCCESS != uhci_wait_for_completion(core)) {
		USB_MSG("UHCI read_data: transfer failed");
		return HCD_READ_ERR;
	}

	/* Calculate total bytes received from completed TDs */
	actual_len = 0;
	for (i = 0; i < core->td_used; i++) {
		td = &(core->td_pool[i]);

		/* Check this TD was an IN transfer */
		if ((td->token & UHCI_TD_TOK_PID_MASK) == UHCI_TD_PID_IN) {
			int td_len;

			/* ActLen is actual_length - 1 (0x7FF = zero) */
			td_len = (td->ctrl_sts & UHCI_TD_CS_ACTLEN_MASK);
			if (td_len == 0x7FF)
				td_len = 0;
			else
				td_len += 1;

			actual_len += td_len;
		}
	}

	/* Copy from internal buffer if needed (for control IN data stage) */
	if ((NULL != buffer) && (actual_len > 0)) {
		/* If data was read into current_rx_buf (control transfer),
		 * copy from there */
		if (core->ep == HCD_DEFAULT_EP) {
			memcpy(buffer, current_rx_buf + current_rx_len,
				actual_len);
			current_rx_len += actual_len;
		}
		/* Otherwise data was placed directly in target buffer
		 * by the buffer_ptr in the TD */
	}

	/* Clean frame list after transfer */
	{
		int j;
		for (j = 0; j < UHCI_FRAMELIST_COUNT; j++)
			core->frame_list[j] = UHCI_FLP_T;
	}

	return actual_len;
}


/*===========================================================================*
 *    uhci_check_error                                                       *
 *===========================================================================*/
int
uhci_check_error(void * cfg, hcd_transfer xfer, hcd_reg1 ep,
		hcd_direction dir)
{
	uhci_core_config * core;
	int i;

	DEBUG_DUMP;

	core = (uhci_core_config *)cfg;

	(void)xfer;
	(void)ep;
	(void)dir;

	/* Wait for completion first */
	if (EXIT_SUCCESS != uhci_wait_for_completion(core))
		return EXIT_FAILURE;

	/* Check all TDs for errors */
	for (i = 0; i < core->td_used; i++) {
		uhci_td * td = &(core->td_pool[i]);

		/* Should not be active anymore */
		if (td->ctrl_sts & UHCI_TD_CS_ACTIVE) {
			USB_MSG("UHCI TD still active after wait");
			return EXIT_FAILURE;
		}

		/* Check error bits */
		if (td->ctrl_sts & UHCI_TD_CS_STALLED) {
			USB_MSG("UHCI STALL on TD %d", i);
			return EXIT_FAILURE;
		}

		if (td->ctrl_sts & UHCI_TD_CS_BABBLE) {
			USB_MSG("UHCI BABBLE on TD %d", i);
			return EXIT_FAILURE;
		}

		if (td->ctrl_sts & UHCI_TD_CS_DBUFFER) {
			USB_MSG("UHCI Data Buffer Error on TD %d", i);
			return EXIT_FAILURE;
		}

		if (td->ctrl_sts & UHCI_TD_CS_BITSTUFF) {
			USB_MSG("UHCI Bitstuff Error on TD %d", i);
			return EXIT_FAILURE;
		}

		if (td->ctrl_sts & UHCI_TD_CS_CRCTO) {
			USB_MSG("UHCI CRC/Timeout on TD %d", i);
			return EXIT_FAILURE;
		}
	}

	/* Update data toggle if applicable */
	if (NULL != core->datatog_tx)
		*(core->datatog_tx) ^= HCD_DATATOG_DATA1;
	if (NULL != core->datatog_rx)
		*(core->datatog_rx) ^= HCD_DATATOG_DATA1;

	return EXIT_SUCCESS;
}
