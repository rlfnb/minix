/*
 * Interface for UHCI (Universal Host Controller Interface) core logic
 *
 * UHCI is an Intel-designed USB 1.1 host controller that uses
 * I/O-space registers and a frame list in main memory for scheduling
 */

#ifndef _UHCI_CORE_H_
#define _UHCI_CORE_H_

#include <usbd/hcd_common.h>


/*===========================================================================*
 *    Types and constants                                                    *
 *===========================================================================*/

/* UHCI Transfer Descriptor - must be 16-byte aligned */
typedef struct uhci_td {
	volatile hcd_reg4 link_ptr;	/* Link Pointer */
	volatile hcd_reg4 ctrl_sts;	/* Control and Status */
	volatile hcd_reg4 token;	/* Token */
	volatile hcd_reg4 buffer_ptr;	/* Buffer Pointer */

	/* Software-only fields (not read by hardware) */
	hcd_reg4 _phys;			/* Physical address of this TD */
	hcd_reg4 _padding[3];		/* Pad to 32 bytes */
} __attribute__((aligned(16)))
uhci_td;

/* UHCI Queue Head - must be 16-byte aligned */
typedef struct uhci_qh {
	volatile hcd_reg4 head_lp;	/* Head Link Pointer */
	volatile hcd_reg4 elem_lp;	/* Element Link Pointer */

	/* Software-only fields */
	hcd_reg4 _phys;			/* Physical address of this QH */
	hcd_reg4 _padding;		/* Pad to 16 bytes */
} __attribute__((aligned(16)))
uhci_qh;

/* Maximum number of TDs we preallocate for transfers */
#define UHCI_MAX_TDS		128
#define UHCI_MAX_QHS		8

/* Structure to hold UHCI core configuration */
typedef struct uhci_core_config {

	/* I/O base address for UHCI registers */
	hcd_reg4 io_base;

	/* Currently used endpoint */
	hcd_reg1 ep;

	/* Currently used device address */
	hcd_reg1 addr;

	/* Data toggle pointers */
	hcd_datatog * datatog_tx;
	hcd_datatog * datatog_rx;

	/* Frame list: 1024 entries, 4K-aligned, physical addresses */
	hcd_reg4 * frame_list;
	hcd_reg4 frame_list_phys;

	/* Preallocated Queue Heads */
	uhci_qh qh_pool[UHCI_MAX_QHS];
	int qh_used;

	/* Preallocated Transfer Descriptors */
	uhci_td td_pool[UHCI_MAX_TDS];
	int td_used;

	/* IRQ number for this controller */
	int irq;

	/* Port connection state tracking */
	int port_connected[2];
}
uhci_core_config;


/*===========================================================================*
 *    Function prototypes                                                    *
 *===========================================================================*/
/* Only to be used outside generic HCD code */
int uhci_core_init(uhci_core_config *);
void uhci_core_start(uhci_core_config *);
void uhci_core_stop(uhci_core_config *);
void uhci_core_reset(uhci_core_config *);

/* For HCD interface */
void uhci_setup_device(void *, hcd_reg1, hcd_reg1,
			hcd_datatog *, hcd_datatog *);
int uhci_reset_device(void *, hcd_speed *);
void uhci_setup_stage(void *, hcd_ctrlrequest *);
void uhci_rx_stage(void *, hcd_datarequest *);
void uhci_tx_stage(void *, hcd_datarequest *);
void uhci_in_data_stage(void *);
void uhci_out_data_stage(void *);
void uhci_in_status_stage(void *);
void uhci_out_status_stage(void *);
int uhci_read_data(void *, hcd_reg1 *, hcd_reg1);
int uhci_check_error(void *, hcd_transfer, hcd_reg1, hcd_direction);

/* ISR-safe I/O accessors (used from uhci_pci.c interrupt handler) */
hcd_reg2 uhci_reg_read16_isr(uhci_core_config *, hcd_reg4);
void uhci_reg_write16_isr(uhci_core_config *, hcd_reg4, hcd_reg2);


#endif /* !_UHCI_CORE_H_ */
