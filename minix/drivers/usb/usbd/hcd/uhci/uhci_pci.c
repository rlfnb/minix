/*
 * UHCI PCI-specific initialization
 *
 * Scans the PCI bus for UHCI host controllers (USB class 0x0C,
 * subclass 0x03, programming interface 0x00), configures I/O space
 * access, and sets up interrupt handling for the found controller.
 */

#include <string.h>				/* memset */

#include <minix/drivers.h>			/* standard driver includes */
#include <minix/syslib.h>			/* PCI functions, sys_privctl */
#include <machine/pci.h>			/* PCI register definitions */

#include <usbd/hcd_common.h>
#include <usbd/hcd_platforms.h>
#include <usbd/hcd_interface.h>
#include <usbd/usbd_common.h>

#include "uhci_core.h"
#include "uhci_regs.h"


/*===========================================================================*
 *    Local declarations                                                     *
 *===========================================================================*/
/* Interrupt handlers */
static void uhci_irq_init(void *);
static void uhci_irq_handler(void *);

/* Port polling for connect/disconnect events */
static void uhci_port_poll(uhci_core_config *, hcd_driver_state *);


/*===========================================================================*
 *    UHCI PCI configuration structure                                       *
 *===========================================================================*/
typedef struct uhci_pci_config {

	uhci_core_config	core;
	hcd_driver_state	driver;
	int			devind;		/* PCI device index */
	int			irq;		/* IRQ number */
	int			found;		/* Controller found flag */
}
uhci_pci_config;

/* Global config holder for one UHCI controller */
static uhci_pci_config uhci_cfg;


/*===========================================================================*
 *    uhci_pci_init                                                          *
 *===========================================================================*/
int
uhci_pci_init(void)
{
	int devind;
	u16_t vid, did;
	u8_t base_class, sub_class, prog_if;
	u32_t io_base;
	u32_t io_size;
	int io_flag;
	int r;
	u16_t cr;

	DEBUG_DUMP;

	/* Clear configuration */
	memset(&uhci_cfg, 0, sizeof(uhci_cfg));

	/* Initialize PCI library */
	pci_init();

	/* Scan PCI bus for UHCI controllers */
	r = pci_first_dev(&devind, &vid, &did);
	while (r == 1) {
		/* Read class code */
		base_class = pci_attr_r8(devind, PCI_BCR);
		sub_class = pci_attr_r8(devind, PCI_SCR);
		prog_if = pci_attr_r8(devind, PCI_PIFR);

		/* Check for USB UHCI controller:
		 * Class 0x0C (Serial Bus), SubClass 0x03 (USB),
		 * ProgIf 0x00 (UHCI) */
		if ((base_class == UHCI_PCI_CLASS) &&
		    (sub_class == UHCI_PCI_SUBCLASS) &&
		    (prog_if == UHCI_PCI_INTERFACE)) {

			USB_MSG("Found UHCI controller: "
				"vendor 0x%04X device 0x%04X",
				vid, did);

			uhci_cfg.devind = devind;
			uhci_cfg.found = 1;
			break;
		}

		r = pci_next_dev(&devind, &vid, &did);
	}

	if (!uhci_cfg.found) {
		USB_MSG("No UHCI controller found on PCI bus");
		return EXIT_FAILURE;
	}

	/* Reserve the PCI device */
	pci_reserve(devind);

	/* UHCI uses BAR4 (offset 0x20) for I/O space per UHCI spec */
	r = pci_get_bar(devind, PCI_BAR_5, &io_base, &io_size, &io_flag);
	if (r != OK || !(io_flag & PCI_BAR_IO)) {
		/* Try BAR0 as fallback - some controllers use it */
		r = pci_get_bar(devind, PCI_BAR, &io_base, &io_size, &io_flag);
		if (r != OK || !(io_flag & PCI_BAR_IO)) {
			USB_MSG("Failed to get I/O BAR for UHCI");
			return EXIT_FAILURE;
		}
	}

	uhci_cfg.core.io_base = io_base;

	USB_MSG("UHCI I/O base: 0x%04X, size: %u",
		(unsigned int)io_base, (unsigned int)io_size);

	/* Get IRQ */
	uhci_cfg.irq = pci_attr_r8(devind, PCI_ILR);
	uhci_cfg.core.irq = uhci_cfg.irq;

	USB_MSG("UHCI IRQ: %d", uhci_cfg.irq);

	/* Enable bus mastering */
	cr = pci_attr_r16(devind, PCI_CR);
	if (!(cr & PCI_CR_MAST_EN))
		pci_attr_w16(devind, PCI_CR, cr | PCI_CR_MAST_EN);

	/* Enable I/O space access */
	cr = pci_attr_r16(devind, PCI_CR);
	if (!(cr & PCI_CR_IO_EN))
		pci_attr_w16(devind, PCI_CR, cr | PCI_CR_IO_EN);

	/* Request I/O port access privilege */
	{
		struct minix_mem_range mr;
		mr.mr_base = io_base;
		mr.mr_limit = io_base + io_size;

		if (sys_privctl(SELF, SYS_PRIV_ADD_IO, &mr) != OK) {
			USB_MSG("Failed to acquire I/O port privilege");
			return EXIT_FAILURE;
		}
	}

	/* Disable PCI legacy support (takes over from BIOS) */
	pci_attr_w16(devind, UHCI_PCI_LEGSUP, UHCI_PCI_LEGSUP_DEFAULT);

	/* Initialize UHCI core (allocates frame list, etc.) */
	if (EXIT_SUCCESS != uhci_core_init(&uhci_cfg.core)) {
		USB_MSG("UHCI core initialization failed");
		return EXIT_FAILURE;
	}

	/* Reset the controller */
	uhci_core_reset(&uhci_cfg.core);

	/* Attach interrupt handler */
	if (EXIT_SUCCESS != hcd_os_interrupt_attach(uhci_cfg.irq,
						uhci_irq_init,
						uhci_irq_handler,
						&uhci_cfg)) {
		USB_MSG("Failed to attach UHCI interrupt");
		return EXIT_FAILURE;
	}

	/* Set up the HCD driver state function pointers */
	uhci_cfg.driver.private_data	= &(uhci_cfg.core);
	uhci_cfg.driver.setup_device	= uhci_setup_device;
	uhci_cfg.driver.reset_device	= uhci_reset_device;
	uhci_cfg.driver.setup_stage	= uhci_setup_stage;
	uhci_cfg.driver.rx_stage	= uhci_rx_stage;
	uhci_cfg.driver.tx_stage	= uhci_tx_stage;
	uhci_cfg.driver.in_data_stage	= uhci_in_data_stage;
	uhci_cfg.driver.out_data_stage	= uhci_out_data_stage;
	uhci_cfg.driver.in_status_stage	= uhci_in_status_stage;
	uhci_cfg.driver.out_status_stage = uhci_out_status_stage;
	uhci_cfg.driver.read_data	= uhci_read_data;
	uhci_cfg.driver.check_error	= uhci_check_error;
	uhci_cfg.driver.port_device	= NULL;

	/* Enable interrupt handling in the OS */
	hcd_os_interrupt_enable(uhci_cfg.irq);

	/* Start the UHCI controller */
	uhci_core_start(&uhci_cfg.core);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    uhci_pci_deinit                                                        *
 *===========================================================================*/
void
uhci_pci_deinit(void)
{
	DEBUG_DUMP;

	if (!uhci_cfg.found)
		return;

	/* Stop the controller */
	uhci_core_stop(&uhci_cfg.core);

	/* Disable interrupt */
	hcd_os_interrupt_disable(uhci_cfg.irq);
	hcd_os_interrupt_detach(uhci_cfg.irq);

	/* Free frame list */
	if (NULL != uhci_cfg.core.frame_list) {
		free_contig(uhci_cfg.core.frame_list,
			UHCI_FRAMELIST_COUNT * sizeof(hcd_reg4));
		uhci_cfg.core.frame_list = NULL;
	}
}


/*===========================================================================*
 *    uhci_irq_init                                                          *
 *===========================================================================*/
static void
uhci_irq_init(void * UNUSED(priv))
{
	DEBUG_DUMP;

	/* DDEKit requires a non-NULL init function but we have
	 * nothing extra to do here */
}


/*===========================================================================*
 *    uhci_irq_handler                                                       *
 *===========================================================================*/
static void
uhci_irq_handler(void * priv)
{
	uhci_pci_config * cfg;
	hcd_reg2 status;

	DEBUG_DUMP;

	cfg = (uhci_pci_config *)priv;

	/* Read status register */
	status = uhci_reg_read16_isr(&cfg->core, UHCI_REG_USBSTS);

	/* Check if this interrupt is for us */
	if (0 == (status & UHCI_STS_ALLINTRS))
		return;

	/* Acknowledge all pending status bits (write-to-clear) */
	uhci_reg_write16_isr(&cfg->core, UHCI_REG_USBSTS,
		status & UHCI_STS_ALLINTRS);

	/* Check for host system error */
	if (status & UHCI_STS_HSE) {
		USB_MSG("UHCI: Host System Error!");
		return;
	}

	/* Check for HC process error */
	if (status & UHCI_STS_HCPE) {
		USB_MSG("UHCI: Host Controller Process Error!");
		return;
	}

	/* Handle port changes: poll ports for connect/disconnect */
	uhci_port_poll(&cfg->core, &cfg->driver);

	/* Handle USB interrupt (transfer completion) */
	if (status & (UHCI_STS_USBINT | UHCI_STS_ERROR)) {
		/* Signal endpoint event to wake any waiting device threads */
		if (NULL != cfg->driver.port_device)
			hcd_handle_event(cfg->driver.port_device,
				HCD_EVENT_ENDPOINT, HCD_DEFAULT_EP);
	}
}


/*===========================================================================*
 *    uhci_port_poll                                                         *
 *===========================================================================*/
static void
uhci_port_poll(uhci_core_config * core, hcd_driver_state * driver)
{
	hcd_reg2 portsc;
	int port;

	for (port = 0; port < UHCI_NUM_PORTS; port++) {
		portsc = uhci_reg_read16_isr(core,
			(0 == port) ? UHCI_REG_PORTSC0 : UHCI_REG_PORTSC1);

		/* Check for connect status change */
		if (portsc & UHCI_PORTSC_CSC) {
			/* Clear the change bit */
			uhci_reg_write16_isr(core,
				(0 == port) ? UHCI_REG_PORTSC0 :
					UHCI_REG_PORTSC1,
				portsc | UHCI_PORTSC_CSC);

			if (portsc & UHCI_PORTSC_CCS) {
				/* Device connected */
				if (!core->port_connected[port]) {
					core->port_connected[port] = 1;
					USB_DBG("UHCI: Device connected "
						"on port %d", port);
					hcd_update_port(driver,
						HCD_EVENT_CONNECTED);
					hcd_handle_event(
						driver->port_device,
						HCD_EVENT_CONNECTED,
						HCD_UNUSED_VAL);
				}
			} else {
				/* Device disconnected */
				if (core->port_connected[port]) {
					core->port_connected[port] = 0;
					USB_DBG("UHCI: Device disconnected "
						"from port %d", port);
					if (NULL != driver->port_device) {
						hcd_handle_event(
							driver->port_device,
							HCD_EVENT_DISCONNECTED,
							HCD_UNUSED_VAL);
						hcd_update_port(driver,
							HCD_EVENT_DISCONNECTED);
					}
				}
			}
		}
	}
}


/*===========================================================================*
 *    ISR-safe I/O accessors (avoid ddekit threading issues)                 *
 *===========================================================================*/
/* These are duplicated here since the core's static accessors
 * cannot be called from this file directly */
hcd_reg2
uhci_reg_read16_isr(uhci_core_config * cfg, hcd_reg4 reg)
{
	u32_t value;

	if (sys_inw(cfg->io_base + reg, &value) != OK)
		return 0;
	return (hcd_reg2)(value & 0xFFFF);
}

void
uhci_reg_write16_isr(uhci_core_config * cfg, hcd_reg4 reg, hcd_reg2 val)
{
	sys_outw(cfg->io_base + reg, val);
}
