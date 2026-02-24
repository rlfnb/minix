/*
 * UHCI (Universal Host Controller Interface) register definitions
 *
 * Based on the Intel UHCI Design Guide, Revision 1.1
 * UHCI is a PCI-based USB 1.1 host controller
 */

#ifndef _UHCI_REGS_H_
#define _UHCI_REGS_H_

/*===========================================================================*
 *    PCI configuration                                                      *
 *===========================================================================*/
/* USB class code in PCI configuration space */
#define UHCI_PCI_CLASS			0x0Cu	/* Serial Bus Controller */
#define UHCI_PCI_SUBCLASS		0x03u	/* USB Controller */
#define UHCI_PCI_INTERFACE		0x00u	/* UHCI */

/* PCI Legacy Support Register (in PCI config space) */
#define UHCI_PCI_LEGSUP			0xC0u
#define UHCI_PCI_LEGSUP_DEFAULT		0x2000u


/*===========================================================================*
 *    UHCI I/O space registers (offsets from I/O base)                       *
 *===========================================================================*/
/* USB Command Register (16-bit, R/W) */
#define UHCI_REG_USBCMD			0x00u
/* USB Status Register (16-bit, R/WC) */
#define UHCI_REG_USBSTS			0x02u
/* USB Interrupt Enable Register (16-bit, R/W) */
#define UHCI_REG_USBINTR		0x04u
/* Frame Number Register (16-bit, R/W) */
#define UHCI_REG_FRNUM			0x06u
/* Frame List Base Address Register (32-bit, R/W) */
#define UHCI_REG_FLBASEADD		0x08u
/* Start Of Frame Modify Register (8-bit, R/W) */
#define UHCI_REG_SOFMOD			0x0Cu
/* Port Status and Control Register 0 (16-bit, R/WC) */
#define UHCI_REG_PORTSC0		0x10u
/* Port Status and Control Register 1 (16-bit, R/WC) */
#define UHCI_REG_PORTSC1		0x12u


/*===========================================================================*
 *    USBCMD register bits                                                   *
 *===========================================================================*/
#define UHCI_CMD_RS			HCD_BIT(0)  /* Run/Stop */
#define UHCI_CMD_HCRESET		HCD_BIT(1)  /* Host Controller Reset */
#define UHCI_CMD_GRESET			HCD_BIT(2)  /* Global Reset */
#define UHCI_CMD_EGSM			HCD_BIT(3)  /* Enter Global Suspend */
#define UHCI_CMD_FGR			HCD_BIT(4)  /* Force Global Resume */
#define UHCI_CMD_SWDBG			HCD_BIT(5)  /* Software Debug */
#define UHCI_CMD_CF			HCD_BIT(6)  /* Configure Flag */
#define UHCI_CMD_MAXP			HCD_BIT(7)  /* Max Packet (64 bytes) */


/*===========================================================================*
 *    USBSTS register bits                                                   *
 *===========================================================================*/
#define UHCI_STS_USBINT			HCD_BIT(0)  /* USB Interrupt */
#define UHCI_STS_ERROR			HCD_BIT(1)  /* USB Error Interrupt */
#define UHCI_STS_RD			HCD_BIT(2)  /* Resume Detect */
#define UHCI_STS_HSE			HCD_BIT(3)  /* Host System Error */
#define UHCI_STS_HCPE			HCD_BIT(4)  /* HC Process Error */
#define UHCI_STS_HCH			HCD_BIT(5)  /* HC Halted */

/* Mask of all writable status bits (write-to-clear) */
#define UHCI_STS_ALLINTRS		(UHCI_STS_USBINT	| \
					 UHCI_STS_ERROR		| \
					 UHCI_STS_RD		| \
					 UHCI_STS_HSE		| \
					 UHCI_STS_HCPE)


/*===========================================================================*
 *    USBINTR register bits                                                  *
 *===========================================================================*/
#define UHCI_INTR_TOCRCIE		HCD_BIT(0)  /* Timeout/CRC Interrupt */
#define UHCI_INTR_RIE			HCD_BIT(1)  /* Resume Interrupt */
#define UHCI_INTR_IOCE			HCD_BIT(2)  /* IOC Interrupt */
#define UHCI_INTR_SPIE			HCD_BIT(3)  /* Short Packet Interrupt */

/* All interrupts enabled */
#define UHCI_INTR_ALL			(UHCI_INTR_TOCRCIE	| \
					 UHCI_INTR_RIE		| \
					 UHCI_INTR_IOCE		| \
					 UHCI_INTR_SPIE)


/*===========================================================================*
 *    PORTSC register bits                                                   *
 *===========================================================================*/
#define UHCI_PORTSC_CCS			HCD_BIT(0)  /* Current Connect Status */
#define UHCI_PORTSC_CSC			HCD_BIT(1)  /* Connect Status Change */
#define UHCI_PORTSC_PE			HCD_BIT(2)  /* Port Enable */
#define UHCI_PORTSC_PECH		HCD_BIT(3)  /* Port Enable Change */
#define UHCI_PORTSC_LSL			HCD_BIT(4)  /* Line Status bit 0 (D+)*/
#define UHCI_PORTSC_LSH			HCD_BIT(5)  /* Line Status bit 1 (D-)*/
#define UHCI_PORTSC_RD			HCD_BIT(6)  /* Resume Detect */
#define UHCI_PORTSC_LSDA		HCD_BIT(8)  /* Low Speed Device */
#define UHCI_PORTSC_PR			HCD_BIT(9)  /* Port Reset */
#define UHCI_PORTSC_SUSP		HCD_BIT(12) /* Suspend */

/* Writable bits that are write-to-clear */
#define UHCI_PORTSC_WC_BITS		(UHCI_PORTSC_CSC | UHCI_PORTSC_PECH)


/*===========================================================================*
 *    Frame List                                                             *
 *===========================================================================*/
#define UHCI_FRAMELIST_COUNT		1024
#define UHCI_FRAMELIST_ALIGN		4096

/* Frame list pointer bits */
#define UHCI_FLP_T			HCD_BIT(0)  /* Terminate */
#define UHCI_FLP_QH			HCD_BIT(1)  /* QH (vs TD) select */


/*===========================================================================*
 *    Transfer Descriptor (TD)                                               *
 *===========================================================================*/
/* TD Link Pointer */
#define UHCI_TD_LP_TERM			HCD_BIT(0)  /* Terminate */
#define UHCI_TD_LP_QH			HCD_BIT(1)  /* QH select */
#define UHCI_TD_LP_DEPTH		HCD_BIT(2)  /* Depth/Breadth select */

/* TD Control and Status (DWORD 1) */
#define UHCI_TD_CS_ACTLEN_MASK		0x7FFu       /* Actual Length mask */
#define UHCI_TD_CS_BITSTUFF		HCD_BIT(17)  /* Bitstuff Error */
#define UHCI_TD_CS_CRCTO		HCD_BIT(18)  /* CRC/Timeout Error */
#define UHCI_TD_CS_NAK			HCD_BIT(19)  /* NAK Received */
#define UHCI_TD_CS_BABBLE		HCD_BIT(20)  /* Babble Detected */
#define UHCI_TD_CS_DBUFFER		HCD_BIT(21)  /* Data Buffer Error */
#define UHCI_TD_CS_STALLED		HCD_BIT(22)  /* Stalled */
#define UHCI_TD_CS_ACTIVE		HCD_BIT(23)  /* Active */
#define UHCI_TD_CS_IOC			HCD_BIT(24)  /* Interrupt on Complete */
#define UHCI_TD_CS_IOS			HCD_BIT(25)  /* Isochronous Select */
#define UHCI_TD_CS_LS			HCD_BIT(26)  /* Low Speed Device */
#define UHCI_TD_CS_SPD			HCD_BIT(29)  /* Short Packet Detect */

/* Error count field: bits 27-28 */
#define UHCI_TD_CS_ERRCNT_SHIFT		27
#define UHCI_TD_CS_ERRCNT_MASK		(0x3u << UHCI_TD_CS_ERRCNT_SHIFT)

/* Any error bits */
#define UHCI_TD_CS_ANY_ERROR		(UHCI_TD_CS_BITSTUFF	| \
					 UHCI_TD_CS_CRCTO	| \
					 UHCI_TD_CS_BABBLE	| \
					 UHCI_TD_CS_DBUFFER	| \
					 UHCI_TD_CS_STALLED)

/* TD Token (DWORD 2) */
#define UHCI_TD_TOK_PID_MASK		0xFFu        /* PID mask */
#define UHCI_TD_TOK_DEVADDR_SHIFT	8            /* Device Address */
#define UHCI_TD_TOK_DEVADDR_MASK	(0x7Fu << UHCI_TD_TOK_DEVADDR_SHIFT)
#define UHCI_TD_TOK_ENDPT_SHIFT		15           /* Endpoint */
#define UHCI_TD_TOK_ENDPT_MASK		(0x0Fu << UHCI_TD_TOK_ENDPT_SHIFT)
#define UHCI_TD_TOK_DATATOG		HCD_BIT(19)  /* Data Toggle */
#define UHCI_TD_TOK_MAXLEN_SHIFT	21           /* Maximum Length */
#define UHCI_TD_TOK_MAXLEN_MASK		(0x7FFu << UHCI_TD_TOK_MAXLEN_SHIFT)

/* PID values */
#define UHCI_TD_PID_SETUP		0x2Du
#define UHCI_TD_PID_IN			0x69u
#define UHCI_TD_PID_OUT			0xE1u

/* Encode MaxLen: actual_length - 1 for the field, 0x7FF means zero length */
#define UHCI_TD_MAXLEN_ENCODE(len)	\
	(((len) > 0) ? (((len) - 1) << UHCI_TD_TOK_MAXLEN_SHIFT) : \
	(0x7FFu << UHCI_TD_TOK_MAXLEN_SHIFT))


/*===========================================================================*
 *    Queue Head (QH)                                                        *
 *===========================================================================*/
/* QH Head Link Pointer */
#define UHCI_QH_HLP_TERM		HCD_BIT(0)  /* Terminate */
#define UHCI_QH_HLP_QH			HCD_BIT(1)  /* QH (vs TD) select */

/* QH Element Link Pointer */
#define UHCI_QH_ELP_TERM		HCD_BIT(0)  /* Terminate */
#define UHCI_QH_ELP_QH			HCD_BIT(1)  /* QH (vs TD) select */


/*===========================================================================*
 *    Misc                                                                   *
 *===========================================================================*/
/* Number of root hub ports on a UHCI controller */
#define UHCI_NUM_PORTS			2

/* UHCI uses 10ms frame interval */
#define UHCI_FRAME_INTERVAL_MS		1


#endif /* !_UHCI_REGS_H_ */
