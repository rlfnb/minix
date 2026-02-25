/*
 * UHCI HCD - Hardware abstraction layer
 *
 * Data structures and function prototypes for UHCI hardware operations.
 * Derived from the original usbd UHCI core, with DDEKit dependencies removed.
 */

#ifndef _UHCI_HW_H_
#define _UHCI_HW_H_

#include <sys/types.h>
#include <minix/usb_ch9.h>

/*===========================================================================*
 *    Register types (replacing hcd_common.h types)                          *
 *===========================================================================*/
#define HCD_BIT(num)            (0x01u << (num))

typedef unsigned long           hcd_reg4;
typedef unsigned short          hcd_reg2;
typedef unsigned char           hcd_reg1;
typedef unsigned long           hcd_addr;

/* Data toggle values */
#define DATATOG_DATA0           0
#define DATATOG_DATA1           1

/* USB speeds */
#define UHCI_SPEED_LOW          0
#define UHCI_SPEED_FULL         1

/* Default USB parameters */
#define UHCI_DEFAULT_EP         0x00u
#define UHCI_DEFAULT_ADDR       0x00u
#define UHCI_MAX_PACKET_LS      8u
#define UHCI_MAX_PACKET_FS      64u

/* Maximum transfer size */
#define UHCI_MAX_XFER_SIZE      1024u


/*===========================================================================*
 *    UHCI hardware register offsets (from I/O base)                         *
 *===========================================================================*/
#define UHCI_REG_USBCMD         0x00u
#define UHCI_REG_USBSTS         0x02u
#define UHCI_REG_USBINTR        0x04u
#define UHCI_REG_FRNUM          0x06u
#define UHCI_REG_FLBASEADD      0x08u
#define UHCI_REG_SOFMOD         0x0Cu
#define UHCI_REG_PORTSC0        0x10u
#define UHCI_REG_PORTSC1        0x12u

/* USBCMD bits */
#define UHCI_CMD_RS             HCD_BIT(0)
#define UHCI_CMD_HCRESET        HCD_BIT(1)
#define UHCI_CMD_GRESET         HCD_BIT(2)
#define UHCI_CMD_CF             HCD_BIT(6)
#define UHCI_CMD_MAXP           HCD_BIT(7)

/* USBSTS bits */
#define UHCI_STS_USBINT         HCD_BIT(0)
#define UHCI_STS_ERROR          HCD_BIT(1)
#define UHCI_STS_RD             HCD_BIT(2)
#define UHCI_STS_HSE            HCD_BIT(3)
#define UHCI_STS_HCPE           HCD_BIT(4)
#define UHCI_STS_HCH            HCD_BIT(5)
#define UHCI_STS_ALLINTRS       (UHCI_STS_USBINT | UHCI_STS_ERROR | \
				 UHCI_STS_RD | UHCI_STS_HSE | UHCI_STS_HCPE)

/* USBINTR bits */
#define UHCI_INTR_TOCRCIE       HCD_BIT(0)
#define UHCI_INTR_RIE           HCD_BIT(1)
#define UHCI_INTR_IOCE          HCD_BIT(2)
#define UHCI_INTR_SPIE          HCD_BIT(3)
#define UHCI_INTR_ALL           (UHCI_INTR_TOCRCIE | UHCI_INTR_RIE | \
				 UHCI_INTR_IOCE | UHCI_INTR_SPIE)

/* PORTSC bits */
#define UHCI_PORTSC_CCS         HCD_BIT(0)
#define UHCI_PORTSC_CSC         HCD_BIT(1)
#define UHCI_PORTSC_PE          HCD_BIT(2)
#define UHCI_PORTSC_PECH        HCD_BIT(3)
#define UHCI_PORTSC_LSDA        HCD_BIT(8)
#define UHCI_PORTSC_PR          HCD_BIT(9)
#define UHCI_PORTSC_SUSP        HCD_BIT(12)
#define UHCI_PORTSC_WC_BITS     (UHCI_PORTSC_CSC | UHCI_PORTSC_PECH)

/* Frame list */
#define UHCI_FRAMELIST_COUNT    1024
#define UHCI_FLP_T              HCD_BIT(0)
#define UHCI_FLP_QH             HCD_BIT(1)

/* TD link pointer bits */
#define UHCI_TD_LP_TERM         HCD_BIT(0)
#define UHCI_TD_LP_QH           HCD_BIT(1)
#define UHCI_TD_LP_DEPTH        HCD_BIT(2)

/* TD control/status bits */
#define UHCI_TD_CS_ACTLEN_MASK  0x7FFu
#define UHCI_TD_CS_BITSTUFF     HCD_BIT(17)
#define UHCI_TD_CS_CRCTO        HCD_BIT(18)
#define UHCI_TD_CS_NAK          HCD_BIT(19)
#define UHCI_TD_CS_BABBLE       HCD_BIT(20)
#define UHCI_TD_CS_DBUFFER      HCD_BIT(21)
#define UHCI_TD_CS_STALLED      HCD_BIT(22)
#define UHCI_TD_CS_ACTIVE       HCD_BIT(23)
#define UHCI_TD_CS_IOC          HCD_BIT(24)
#define UHCI_TD_CS_IOS          HCD_BIT(25)
#define UHCI_TD_CS_LS           HCD_BIT(26)
#define UHCI_TD_CS_SPD          HCD_BIT(29)
#define UHCI_TD_CS_ERRCNT_SHIFT 27
#define UHCI_TD_CS_ANY_ERROR    (UHCI_TD_CS_BITSTUFF | UHCI_TD_CS_CRCTO | \
				 UHCI_TD_CS_BABBLE | UHCI_TD_CS_DBUFFER | \
				 UHCI_TD_CS_STALLED)

/* TD token bits */
#define UHCI_TD_TOK_PID_MASK    0xFFu
#define UHCI_TD_TOK_DEVADDR_SHIFT 8
#define UHCI_TD_TOK_ENDPT_SHIFT 15
#define UHCI_TD_TOK_DATATOG     HCD_BIT(19)
#define UHCI_TD_TOK_MAXLEN_SHIFT 21

/* PID values */
#define UHCI_TD_PID_SETUP       0x2Du
#define UHCI_TD_PID_IN          0x69u
#define UHCI_TD_PID_OUT         0xE1u

/* Encode MaxLen: actual_length - 1 for the field */
#define UHCI_TD_MAXLEN_ENCODE(len) \
	(((len) > 0) ? (((len) - 1) << UHCI_TD_TOK_MAXLEN_SHIFT) : \
	(0x7FFu << UHCI_TD_TOK_MAXLEN_SHIFT))

/* QH pointer bits */
#define UHCI_QH_HLP_TERM       HCD_BIT(0)
#define UHCI_QH_HLP_QH         HCD_BIT(1)
#define UHCI_QH_ELP_TERM       HCD_BIT(0)

/* Number of root hub ports */
#define UHCI_NUM_PORTS          2

/* PCI identification */
#define UHCI_PCI_CLASS          0x0Cu
#define UHCI_PCI_SUBCLASS       0x03u
#define UHCI_PCI_INTERFACE      0x00u
#define UHCI_PCI_LEGSUP         0xC0u
#define UHCI_PCI_LEGSUP_DEFAULT 0x2000u


/*===========================================================================*
 *    Hardware data structures                                               *
 *===========================================================================*/

/* Maximum number of TDs/QHs we preallocate */
#define UHCI_MAX_TDS            128
#define UHCI_MAX_QHS            8

/* UHCI Transfer Descriptor - must be 16-byte aligned */
typedef struct uhci_td {
	volatile hcd_reg4 link_ptr;
	volatile hcd_reg4 ctrl_sts;
	volatile hcd_reg4 token;
	volatile hcd_reg4 buffer_ptr;

	/* Software-only fields */
	hcd_reg4 _phys;
	hcd_reg4 _padding[3];
} __attribute__((aligned(16)))
uhci_td;

/* UHCI Queue Head - must be 16-byte aligned */
typedef struct uhci_qh {
	volatile hcd_reg4 head_lp;
	volatile hcd_reg4 elem_lp;

	/* Software-only fields */
	hcd_reg4 _phys;
	hcd_reg4 _padding;
} __attribute__((aligned(16)))
uhci_qh;

/* UHCI controller state */
typedef struct uhci_state {
	/* PCI and I/O */
	hcd_reg4        io_base;
	int             devind;
	int             irq;
	int             irq_hook_id;

	/* Frame list (4K-aligned) */
	hcd_reg4       *frame_list;
	hcd_reg4        frame_list_phys;

	/* Preallocated pools */
	uhci_qh         qh_pool[UHCI_MAX_QHS];
	int             qh_used;
	uhci_td         td_pool[UHCI_MAX_TDS];
	int             td_used;

	/* Current transfer state */
	hcd_reg1        cur_ep;
	hcd_reg1        cur_addr;
	int             cur_datatog;

	/* Port status tracking */
	int             port_connected[UHCI_NUM_PORTS];

	/* Receive buffer for control transfers */
	hcd_reg1        rx_buf[UHCI_MAX_XFER_SIZE];
	int             rx_len;
} uhci_state;


/*===========================================================================*
 *    Function prototypes                                                    *
 *===========================================================================*/

/* Initialization */
int  uhci_hw_init(uhci_state *uhci);
void uhci_hw_reset(uhci_state *uhci);
void uhci_hw_start(uhci_state *uhci);
void uhci_hw_stop(uhci_state *uhci);

/* Port management */
int  uhci_port_reset(uhci_state *uhci, int port, int *speed);
int  uhci_port_read_status(uhci_state *uhci, int port);

/* Transfer execution (synchronous, poll-based) */
int  uhci_control_transfer(uhci_state *uhci, int addr, int ep,
	usb_device_request_t *setup, hcd_reg1 *data, int data_len,
	int direction, int speed);

int  uhci_interrupt_transfer(uhci_state *uhci, int addr, int ep,
	hcd_reg1 *data, int data_len, int direction, int speed,
	int max_packet_size);

/* I/O access */
hcd_reg2 uhci_reg_read16(uhci_state *uhci, hcd_reg4 reg);
void     uhci_reg_write16(uhci_state *uhci, hcd_reg4 reg, hcd_reg2 val);

#endif /* !_UHCI_HW_H_ */
