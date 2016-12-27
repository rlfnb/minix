#ifndef _UHCI_H_
#define	_UHCI_H_

#define UHCI_MAX_DEVICES 127

/* Structures alignment (bytes) */
#define UHCI_TD_ALIGN           16
#define UHCI_QH_ALIGN           16

#define UHCI_PTR_T              0x00000001
#define UHCI_PTR_TD             0x00000000
#define UHCI_PTR_QH             0x00000002
#define UHCI_PTR_VF             0x00000004

/*
 * The Queue Heads (QH) and Transfer Descriptors (TD) are accessed by
 * both the CPU and the USB-controller which run concurrently. Great
 * care must be taken. When the data-structures are linked into the
 * USB controller's frame list, the USB-controller "owns" the
 * td_status and qh_elink fields, which will not be written by the
 * CPU.
 */
struct uhci_td {
	/*
     * Data used by the UHCI controller.
     * volatile is used in order to mantain struct members ordering.
     */
	volatile uint32_t td_next;
	volatile uint32_t td_status;
#define UHCI_TD_GET_ACTLEN(s)   (((s) + 1) & 0x3ff)
#define UHCI_TD_ZERO_ACTLEN(t)  ((t) | 0x3ff)
#define UHCI_TD_BITSTUFF        0x00020000
#define UHCI_TD_CRCTO           0x00040000
#define UHCI_TD_NAK             0x00080000
#define UHCI_TD_BABBLE          0x00100000
#define UHCI_TD_DBUFFER         0x00200000
#define UHCI_TD_STALLED         0x00400000
#define UHCI_TD_ACTIVE          0x00800000
#define UHCI_TD_IOC             0x01000000
#define UHCI_TD_IOS             0x02000000
#define UHCI_TD_LS              0x04000000
#define UHCI_TD_GET_ERRCNT(s)   (((s) >> 27) & 3)
#define UHCI_TD_SET_ERRCNT(n)   ((n) << 27)
#define UHCI_TD_SPD             0x20000000
	volatile uint32_t td_token;
#define UHCI_TD_PID             0x000000ff
#define UHCI_TD_PID_IN          0x00000069
#define UHCI_TD_PID_OUT         0x000000e1
#define UHCI_TD_PID_SETUP       0x0000002d
#define UHCI_TD_GET_PID(s)      ((s) & 0xff)
#define UHCI_TD_SET_DEVADDR(a)  ((a) << 8)
#define UHCI_TD_GET_DEVADDR(s)  (((s) >> 8) & 0x7f)
#define UHCI_TD_SET_ENDPT(e)    (((e) & 0xf) << 15)
#define UHCI_TD_GET_ENDPT(s)    (((s) >> 15) & 0xf)
#define UHCI_TD_SET_DT(t)       ((t) << 19)
#define UHCI_TD_GET_DT(s)       (((s) >> 19) & 1)
#define UHCI_TD_SET_MAXLEN(l)   (((l)-1U) << 21)
#define UHCI_TD_GET_MAXLEN(s)   ((((s) >> 21) + 1) & 0x7ff)
#define UHCI_TD_MAXLEN_MASK     0xffe00000
	volatile uint32_t td_buffer;
	/*
     * Extra information needed:
     */
	struct uhci_td *next;
	struct uhci_td *prev;
	struct uhci_td *obj_next;
	struct usb_page_cache *page_cache;
	struct usb_page_cache *fix_pc;
	uint32_t td_self;
    uint16_t len;
} __aligned(UHCI_TD_ALIGN);

typedef struct uhci_td uhci_td_t;

#define UHCI_TD_ERROR   (UHCI_TD_BITSTUFF | UHCI_TD_CRCTO |             \
                         UHCI_TD_BABBLE | UHCI_TD_DBUFFER | UHCI_TD_STALLED)

#define UHCI_TD_SETUP(len, endp, dev)   (UHCI_TD_SET_MAXLEN(len) |      \
                                         UHCI_TD_SET_ENDPT(endp) |       \
                                         UHCI_TD_SET_DEVADDR(dev) |      \
                                         UHCI_TD_PID_SETUP)

#define UHCI_TD_OUT(len, endp, dev, dt) (UHCI_TD_SET_MAXLEN(len) |      \
                                         UHCI_TD_SET_ENDPT(endp) |       \
                                         UHCI_TD_SET_DEVADDR(dev) |      \
                                         UHCI_TD_PID_OUT | UHCI_TD_SET_DT(dt))

#define UHCI_TD_IN(len, endp, dev, dt)  (UHCI_TD_SET_MAXLEN(len) |      \
                                         UHCI_TD_SET_ENDPT(endp) |       \
                                         UHCI_TD_SET_DEVADDR(dev) |      \
                                         UHCI_TD_PID_IN | UHCI_TD_SET_DT(dt))

struct uhci_qh {
/*
 * Data used by the UHCI controller.
 */
	volatile uint32_t qh_h_next;
	volatile uint32_t qh_e_next;
/*
 * Extra information needed:
 */
	struct uhci_qh *h_next;
	struct uhci_qh *h_prev;
	struct uhci_qh *obj_next;
	struct uhci_td *e_next;
	struct usb_page_cache *page_cache;
	uint32_t qh_self;
	uint16_t intr_pos;
} __aligned(UHCI_QH_ALIGN);

typedef struct uhci_qh uhci_qh_t;

/* Maximum number of isochronous TD's and QH's interrupt */
#define UHCI_VFRAMELIST_COUNT   128
#define UHCI_IFRAMELIST_COUNT   256

struct uhci_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union uhci_hub_desc {
	struct usb_status stat;
	struct usb_port_status ps;
	uint8_t temp[128];
};

/*
 * The following structure defines physical and non kernel virtual
 * address of a memory page having size USB_PAGE_SIZE.
 */
struct usb_page {
	phys_bytes physaddr;
	void   *buffer;			/* non Kernel Virtual Address */
};

/*
 * The following structure is used when needing the kernel virtual
 * pointer and the physical address belonging to an offset in an USB
 * page cache.
 */
struct usb_page_search {
	void *buffer;
	u32_t *physaddr;
	u16_t length;
};

/*
 * The following structure is used to keep information about a DMA
 * memory allocation.
 */
struct usb_page_cache {

	struct usb_page *page_start;
	void   *buffer;			/* virtual buffer pointer */
	u16_t page_offset_buf;
	u16_t page_offset_end;
	uint8_t	isread:1;		/* set if we are currently reading
					 * from the memory. Else write. */
	uint8_t	ismultiseg:1;		/* set if we can have multiple
					 * segments */
};

struct uhci_hw_softc {
	struct usb_page_cache pframes_pc;
	struct usb_page_cache isoc_start_pc[UHCI_VFRAMELIST_COUNT];
	struct usb_page_cache intr_start_pc[UHCI_IFRAMELIST_COUNT];
	struct usb_page_cache ls_ctl_start_pc;
	struct usb_page_cache fs_ctl_start_pc;
	struct usb_page_cache bulk_start_pc;
	struct usb_page_cache last_qh_pc;
	struct usb_page_cache last_td_pc;

    struct usb_page pframes_pg;
    struct usb_page isoc_start_pg[UHCI_VFRAMELIST_COUNT];
    struct usb_page intr_start_pg[UHCI_IFRAMELIST_COUNT];
    struct usb_page ls_ctl_start_pg;
    struct usb_page fs_ctl_start_pg;
    struct usb_page bulk_start_pg;
    struct usb_page last_qh_pg;
    struct usb_page last_td_pg;
};

typedef struct hcd_uhci {
	struct uhci_hw_softc sc_hw;

	struct usb_device *sc_devices[UHCI_MAX_DEVICES];
	/* pointer to last TD for isochronous */
	struct uhci_td *sc_isoc_p_last[UHCI_VFRAMELIST_COUNT];
	/* pointer to last QH for interrupt */
	struct uhci_qh *sc_intr_p_last[UHCI_IFRAMELIST_COUNT];
	/* pointer to last QH for low speed control */
	struct uhci_qh *sc_ls_ctl_p_last;
	/* pointer to last QH for full speed control */
	struct uhci_qh *sc_fs_ctl_p_last;
	/* pointer to last QH for bulk */
	struct uhci_qh *sc_bulk_p_last;
	struct uhci_qh *sc_reclaim_qh_p;
	struct uhci_qh *sc_last_qh_p;
	struct uhci_td *sc_last_td_p;

	unsigned base;
	int devind;
	u16_t did;
	int irq;
	int irq_hook;
	char *regs;
	u32_t size;
	u16_t vid;
	
	struct usb_page_cache usb_page_cache;
} hcd_uhci_t;

#endif					/* _UHCI_H_ */
