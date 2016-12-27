#include <minix/driver.h>
#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/optset.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <machine/pci.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "usb.h"
#include "uhci.h"
#include "uhcireg.h"

static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
// static int sef_cb_lu_state_save(int);
// static int lu_state_restore(void);

static void uhci_dumpregs(void);
static int uhci_interrupt(void);
static int uhci_init_device(void);
static int uhci_reset(void);
static int uhci_start(void);
static void uhci_root_intr(void);
// static struct uhci_qh * uhci_init_qh(void * logical_address, phys_bytes physical_address);

static void uhci_dump_memory(void);

static struct log log = { .name = "uhci", .log_level = LEVEL_TRACE, .log_func =	default_log };

static hcd_uhci_t hcd;

static long uhci_instance = 0;

#define MILLIS_TO_TICKS(m)  (((m)*system_hz/1000)+1)

#define UREAD1(sc, r) uhci_inb(sc, r)
#define UREAD2(sc, r) uhci_inw(sc, r)
#define UREAD4(sc, r) uhci_inl(sc, r)

#define UWRITE1(sc, r, x) uhci_outb((sc.base + r), x)
#define UWRITE2(sc, r, x) uhci_outw((sc.base + r), x)
#define UWRITE4(sc, r, x) uhci_outl((sc.base + r), x)

#define UHCISTS(sc) UREAD2(sc, UHCI_STS)
#define UHCICMD(sc, cmd) UWRITE2(sc, UHCI_CMD, cmd)

static unsigned
uhci_inb(hcd_uhci_t sc, unsigned r) {
	u32_t value;
	int s;
	if ((s=sys_inb(sc.base + r, &value)) !=OK)
		log_warn(&log, "UHCI: warning, sys_inb failed: %d\n", s);
	return value;
}

static unsigned
uhci_inw(hcd_uhci_t sc, unsigned r) {
	u32_t value;
	int s;
	if ((s=sys_inw(sc.base + r, &value)) !=OK)
		log_warn(&log, "UHCI: warning, sys_inw failed: %d\n", s);
	return value;
}

static unsigned
uhci_inl(hcd_uhci_t sc, unsigned r) {
	u32_t value;
	int s;
	if ((s=sys_inl(sc.base + r, &value)) !=OK)
		log_warn(&log, "UHCI: warning, sys_inl failed: %d\n", s);
	return value;
}

static void
uhci_outb(u16_t port, u8_t value) {
	int s;
	if ((s=sys_outb(port, value)) !=OK)
		printf("UHCI: warning, sys_outb failed: %d\n", s);
}

static void
uhci_outw(u16_t port, u16_t value) {
	int s;
	if ((s=sys_outw(port, value)) !=OK)
		printf("UHCI: warning, sys_outw failed: %d\n", s);
}

static void
uhci_outl(u16_t port, u32_t value) {
	int s;
	if ((s=sys_outl(port, value)) !=OK)
		printf("UHCI: warning, sys_outl failed: %d\n", s);
}

static int
uhci_ctrl_get_descriptor(u8_t address, u8_t descriptor_type, ) {
	
}

static void 
uhci_dump_frame_list_addr(void){

	char * base_addr;
	u32_t * framelist_addr = hcd.usb_page_cache.page_start->buffer;
	u16_t _printed_i;
	base_addr = (char *) hcd.usb_page_cache.page_start->buffer;
	_printed_i = 0;

	printf("dump memory (physical address: %04x logical address: %p)\n", (unsigned int)hcd.usb_page_cache.page_start->physaddr, base_addr);
	printf("frame list (1024 entries)");

	for(u16_t i = 0 ; i<1024 ; i++){
		if(framelist_addr[i] != 1){
			if(_printed_i %8 == 0){
				printf("\n");
			}
			printf("[%04d] %08x ", i, framelist_addr[i]);
			_printed_i++;
		}
	}
	printf("\n");
}

static void
uhci_dump_queue_heads(void){
	char * base_addr;
	base_addr = (char *) hcd.usb_page_cache.page_start->buffer;

	uhci_qh_t * queue_head_addr = (uhci_qh_t *) (base_addr +4096);
	printf("dump memory (physical address: %04x logical address: %p)\n", (unsigned int)hcd.usb_page_cache.page_start->physaddr, base_addr);
	printf("queue heads (32 entries, offset 4096, logical_address: %p sizeof(uhci_qh_t): 0x%x)", queue_head_addr, sizeof(uhci_qh_t));
	for(u16_t j = 0 ; j<32 ; j++){
		if(j%2 == 0){
			printf("\n");
		}
		printf("h_next: %08x e_next:%08x  ", queue_head_addr[j].qh_h_next, queue_head_addr[j].qh_e_next);
	}
	printf("\n");
}

static void 
uhci_dump_memory(void){
	uhci_dump_frame_list_addr();
	uhci_dump_queue_heads();
}

static void uhci_detach(int signo){
	log_debug(&log, "received signal number: %d\n", signo);
	if (signo == SIGTERM) {
		log_warn(&log, "detach device!\n");
	}
}

void sef_local_startup() {
	log_info(&log, "called sef_local_startup()\n");
	sef_setcb_init_fresh(sef_cb_init);
	sef_setcb_init_lu(sef_cb_init);
	sef_setcb_init_restart(sef_cb_init);
	sef_setcb_signal_handler(uhci_detach);

    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);

	sef_startup();
	log_info(&log, "called sef_startup()\n");
}

int sef_cb_init(int type, sef_init_info_t * (info)) {
	log_info(&log, "called sef_cb_init() with type: %d\n", type);
	switch (type) {
	case SEF_INIT_FRESH:
		return uhci_init_device();
	case SEF_INIT_LU:
		return OK;
	case SEF_INIT_RESTART:
		uhci_reset();
		return uhci_start();
	default:
 		return OK;
	}
}

static void
uhci_dumpregs(void)
{
	log_warn(&log, "regs: cmd=%04x, sts=%04x, intr=%04x, frnum=%04x, "
				"flbase=%08x, sof=%04x, portsc1=%04x, portsc2=%04x\n",
				UREAD2(hcd, UHCI_CMD),
				UREAD2(hcd, UHCI_STS),
				UREAD2(hcd, UHCI_INTR),
				UREAD2(hcd, UHCI_FRNUM),
				UREAD4(hcd, UHCI_FLBASEADDR),
				UREAD1(hcd, UHCI_SOF),
				UREAD2(hcd, UHCI_PORTSC1),
				UREAD2(hcd, UHCI_PORTSC2));
}

/*
static u8_t
uhci_dump_td(uhci_td_t *p)
{
	u32_t td_next;
	u32_t td_status;
	u32_t td_token;
	u8_t temp;

	td_next = le32toh(p->td_next);
	td_status = le32toh(p->td_status);
	td_token = le32toh(p->td_token);

	// Check whether the link pointer in this TD marks the link pointer as end of queue:
	temp = ((td_next & UHCI_PTR_T) || (td_next == 0));

	printf("TD(%p) at 0x%08x = link=0x%08x status=0x%08x "
			"token=0x%08x buffer=0x%08x\n",
			p,
			le32toh(p->td_self),
			td_next,
			td_status,
			td_token,
			le32toh(p->td_buffer));

	printf("TD(%p) td_next=%s%s%s td_status=%s%s%s%s%s%s%s%s%s%s%s, errcnt=%d, actlen=%d pid=%02x,"
			"addr=%d,endpt=%d,D=%d,maxlen=%d\n",
			p,
			(td_next & 1) ? "-T" : "",
			(td_next & 2) ? "-Q" : "",
			(td_next & 4) ? "-VF" : "",
			(td_status & UHCI_TD_BITSTUFF) ? "-BITSTUFF" : "",
			(td_status & UHCI_TD_CRCTO) ? "-CRCTO" : "",
			(td_status & UHCI_TD_NAK) ? "-NAK" : "",
			(td_status & UHCI_TD_BABBLE) ? "-BABBLE" : "",
			(td_status & UHCI_TD_DBUFFER) ? "-DBUFFER" : "",
			(td_status & UHCI_TD_STALLED) ? "-STALLED" : "",
			(td_status & UHCI_TD_ACTIVE) ? "-ACTIVE" : "",
			(td_status & UHCI_TD_IOC) ? "-IOC" : "",
			(td_status & UHCI_TD_IOS) ? "-IOS" : "",
			(td_status & UHCI_TD_LS) ? "-LS" : "",
			(td_status & UHCI_TD_SPD) ? "-SPD" : "",
			UHCI_TD_GET_ERRCNT(td_status),
			UHCI_TD_GET_ACTLEN(td_status),
			UHCI_TD_GET_PID(td_token),
			UHCI_TD_GET_DEVADDR(td_token),
			UHCI_TD_GET_ENDPT(td_token),
			UHCI_TD_GET_DT(td_token),
			UHCI_TD_GET_MAXLEN(td_token));

	return (temp);
}
*/

static u8_t
uhci_dump_qh(uhci_qh_t *sqh)
{
	u8_t temp;
	temp = 0;
	u32_t qh_h_next;
	u32_t qh_e_next;

	qh_h_next = le32toh(sqh->qh_h_next);
	qh_e_next = le32toh(sqh->qh_e_next);

	log_info(&log, "QH(%p) at 0x%08x: h_next=0x%08x e_next=0x%08x\n", sqh,
				sqh->qh_self, qh_h_next, qh_e_next);

	return (temp);
}

static void
uhci_dump_all(void)
{
	uhci_dumpregs();
	// uhci_dump_qh(hcd.sc_ls_ctl_p_last);
	// uhci_dump_qh(hcd.sc_fs_ctl_p_last);
	// uhci_dump_qh(sc->sc_bulk_p_last);
	// uhci_dump_qh(hcd.sc_last_qh_p);
}

/*
static void
uhci_dump_tds(uhci_td_t *td)
{
	for (;
		td != NULL;
		td = td->obj_next) {
			if (uhci_dump_td(td)) {
				break;
			}
		}
}
*/

static void
uhci_init_framelist()
{
	phys_bytes phys_base_addr;
	u32_t * framelist;

	framelist = hcd.usb_page_cache.page_start->buffer;
	phys_base_addr = hcd.usb_page_cache.page_start->physaddr;

	for (u16_t i = 0 ; i<1024 ; i++){
		if(i%32 == 0){
			framelist[i]= phys_base_addr+4096+i | UHCI_PTR_QH;			
		}else{
			// invalid frame list entry
			framelist[i]=1;
		}
	}

	uhci_qh_t * queue_head;
	queue_head = (uhci_qh_t *) (framelist + 1024);
	for (u8_t j = 0 ; j<32 ; j++){
		queue_head[j].qh_h_next = phys_base_addr+4096+((j+1)*sizeof(uhci_qh_t)) | UHCI_PTR_QH;
		queue_head[j].qh_e_next = 1;
 	}
	queue_head[31].qh_h_next = (phys_base_addr+4096) | UHCI_PTR_QH;

//	hcd.sc_fs_ctl_p_last = uhci_init_qh((void *) (queue_head), hcd.usb_page_cache.page_start->physaddr+4096);
//	hcd.sc_ls_ctl_p_last = uhci_init_qh((void *) (queue_head+1), hcd.usb_page_cache.page_start->physaddr+4096+sizeof(struct uhci_qh)*1);

	// framelist[0] = hcd.sc_fs_ctl_p_last->qh_self;
	uhci_dump_memory();
	log_trace(&log, "UHCI frame list initialised\n");
}

 /*
  * Return values:
  * 0: Success
  * Else: Failure
  */
static u8_t
uhci_restart()
{
	struct usb_page page_start;
	if (UREAD2(hcd, UHCI_CMD) & UHCI_CMD_RS) {
		log_warn(&log, "Already started\n");
		return (0);
	}

	log_info(&log, "Restarting\n");

	hcd.usb_page_cache.page_start = &page_start;

    if ((page_start.buffer = alloc_contig(65536, AC_ALIGN64K, &page_start.physaddr)) == NULL) {
        log_warn(&log, "could not continuously allocate page_cache!\n");
        return -1;
    }

	uhci_init_framelist();

	/* Reload fresh base address */
	UWRITE4(hcd, UHCI_FLBASEADDR, hcd.usb_page_cache.page_start->physaddr);

	/*
	 * Assume 64 byte packets at frame end and start HC controller:
	 */
	UHCICMD(hcd, (UHCI_CMD_MAXP | UHCI_CMD_RS));

	/* wait 10 milliseconds */
	tickdelay(1);

	/* check that controller has started */
	if (UREAD2(hcd, UHCI_STS) & UHCI_STS_HCH) {
		log_warn(&log, "Failed\n");
		return (1);
	}
	return (0);
}

static int
uhci_reset() {
	u16_t n;

	log_trace(&log, "reset UHCI controller\n");

    UWRITE2(hcd, UHCI_INTR, 0);
    /* global reset */
    UHCICMD(hcd, UHCI_CMD_GRESET);
    /* wait */
	tickdelay(10);
    /* terminate all transfers */
    UHCICMD(hcd, UHCI_CMD_HCRESET);
    /* the reset bit goes low when the controller is done */
    n = 60;
    while (n--) {
		/* wait one millisecond */
		tickdelay(1);

		if (!(UREAD2(hcd, UHCI_CMD) & UHCI_CMD_HCRESET)) {
			goto done_1;
		}
	}
	log_warn(&log, "controller did not reset\n");
	done_1:
	n = 10;
	while (n--) {
		/* wait one millisecond */
		tickdelay(1);
		/* check if HC is stopped */
		if (UREAD2(hcd, UHCI_STS) & UHCI_STS_HCH) {
			goto done_2;
		}
	}
	log_warn(&log, "controller did not stop\n");
	done_2:
	/* reset frame number */
	UWRITE2(hcd, UHCI_FRNUM, 0);
	/* set default SOF value */
	UWRITE1(hcd, UHCI_SOF, 0x40);

	log_trace(&log, "after controller reset UHCISTS: %04x  UHCI_SOF_MOD: %04x\n",UHCISTS(hcd), UREAD2(hcd, UHCI_SOF));

	return OK;
}

static int
uhci_start(void)
{
	log_info(&log, "enabling\n");

	/* enable interrupts */
	UWRITE2(hcd, UHCI_INTR,
				(UHCI_INTR_TOCRCIE |
				 UHCI_INTR_RIE |
				 UHCI_INTR_IOCE |
				 UHCI_INTR_SPIE));

	if (uhci_restart()) {
		log_warn(&log, "cannot start HC controller\n");
		return 1;
	}
	/* start root interrupt */
	uhci_root_intr();
	return OK;
}

/*
static struct uhci_qh *
uhci_init_qh(void * logical_address, phys_bytes physical_address)
{
	struct uhci_qh *qh;
	qh = (struct uhci_qh *) logical_address;
	qh->qh_self = physical_address | htole32(UHCI_PTR_QH);

	return (qh);
}
*/

static void
uhci_interrupt_poll()
{

}

int uhci_interrupt() {
	log_trace(&log, "real interrupt\n");
    u32_t status;

	uhci_dumpregs();
	uhci_dump_qh(hcd.sc_fs_ctl_p_last);

	status = UREAD2(hcd, UHCI_STS) & UHCI_STS_ALLINTRS;
	if (status == 0) {
		/* the interrupt was not for us */
		goto done;
	}
	if (status & (UHCI_STS_RD | UHCI_STS_HSE |
					UHCI_STS_HCPE | UHCI_STS_HCH)) {

		if (status & UHCI_STS_RD) {
			log_info(&log, "resume detect\n");
		}
		if (status & UHCI_STS_HSE) {
			log_info(&log,"host system error\n");
		}
		if (status & UHCI_STS_HCPE) {
			log_info(&log,"host controller process error\n");
		}
		if (status & UHCI_STS_HCH) {
			/* no acknowledge needed */
			log_warn(&log,"host controller halted\n");
			uhci_dump_all();
		}
	}
	/* get acknowledge bits */
	status &= (UHCI_STS_USBINT |
				UHCI_STS_USBEI |
				UHCI_STS_RD |
				UHCI_STS_HSE |
				UHCI_STS_HCPE |
				UHCI_STS_HCH);

	if (status == 0) {
		/* nothing to acknowledge */
		goto done;
	}
	/* acknowledge interrupts */
	UWRITE2(hcd, UHCI_STS, status);

	/* poll all the USB transfers */
	uhci_interrupt_poll();

	done:
	return OK;
}

/*
 * This routine is executed periodically and simulates interrupts from
 * the root controller interrupt pipe for port status change:
 */
static void
uhci_root_intr()
{
	log_trace(&log, "\n");
	uhci_dumpregs();
	if (UREAD2(hcd, UHCI_PORTSC1) & (UHCI_PORTSC_CSC |
		UHCI_PORTSC_OCIC | UHCI_PORTSC_RD)) {
			// acknowldge connection change
			UWRITE2(hcd, UHCI_PORTSC1, UHCI_PORTSC_CSC);
			if(UREAD2(hcd, UHCI_PORTSC1) & UHCI_PORTSC_CCS){
				log_info(&log, "connected device\n");
				UWRITE2(hcd, UHCI_PORTSC1, UHCI_PORTSC_PR);
				// roughly 50 ms
				tickdelay(5);
				UWRITE2(hcd, UHCI_PORTSC1, UREAD2(hcd, UHCI_PORTSC1) & ~UHCI_PORTSC_PR);
				for(u8_t i = 0 ; i < 16 ; i++){
					tickdelay(1);
					if(! UHCI_PORTSC1 & UHCI_PORTSC_PR){
						log_info(&log, "port reset successful\n");
						break;
					}
				}
				for(u8_t i = 0 ; i < 16 ; i++){
					UWRITE2(hcd, UHCI_PORTSC1, UHCI_PORTSC_CSC|UHCI_PORTSC_POEDC|UHCI_PORTSC_PE);
					tickdelay(1);
					if(UHCI_PORTSC1 & UHCI_PORTSC_PE){
						break;
					}
				}
				if(UHCI_PORTSC1 & UHCI_PORTSC_PE){
					log_warn(&log, "port NOT enabled\n");
				}
			}else{
				log_info(&log, "disconnected device\n");
			}
			uhci_dumpregs();
	}
	if (UREAD2(hcd, UHCI_PORTSC2) & (UHCI_PORTSC_CSC |
		UHCI_PORTSC_OCIC | UHCI_PORTSC_RD)) {
			uhci_dumpregs();
	}
	/* restart timer */
	sys_setalarm(sys_hz(), FALSE);
}

int uhci_init_device() {
	int r, ioflag;
	env_parse("instance", "d", 0, &uhci_instance, 0, 8);
	log_trace(&log, "instance=%d\n", uhci_instance);
	pci_init();
	log_trace(&log, "pci init done\n");
	r = pci_first_dev(&hcd.devind, &hcd.vid, &hcd.did);
	for(int idx=0;idx<uhci_instance;idx++){
		r = pci_next_dev(&hcd.devind, &hcd.vid, &hcd.did);
	}
	if (r == 1) {
		log_info(&log, "%04x:%04x found!\n", hcd.vid, hcd.did);
	} else {
		log_warn(&log, "could not find device!\n");
		return -1;
	}
	pci_reserve(hcd.devind);
	r = pci_get_bar(hcd.devind, PCI_BAR_5, &hcd.base, &hcd.size, &ioflag);
	if (r != OK) {
		log_warn(&log, "could not get base address register for devind=%d\n",
				hcd.devind);
		return -1;
	} else {
		log_info(&log, "BAR %04x found!\n", hcd.base);
	}

	hcd.irq = pci_attr_r8(hcd.devind, PCI_ILR);
	hcd.irq_hook = 0;
	if ((r = sys_irqsetpolicy(hcd.irq, 0, &hcd.irq_hook)) != OK)
		panic("could not set irq policy!");

	uhci_reset();

	uhci_start();

	return OK;
}

/*===========================================================================*
 *                              main                                         *
 *===========================================================================*/
int main(int argc, char **argv) {
	log_info(&log, "try to start uhci...\n");
	int r, ipc_status;

	message m;
	env_setargs(argc, argv);
	sef_local_startup();

	while (TRUE) {
	    if (hcd.irq != 0)
	        sys_irqenable(&hcd.irq_hook);
		if ((r = driver_receive(ANY, &m, &ipc_status)) != OK)
			panic("driver_receive failed: %d", r);
	    if (hcd.irq != 0)
	        sys_irqdisable(&hcd.irq_hook);
		if (is_ipc_notify(ipc_status)) {
			switch (_ENDPOINT_P(m.m_source)) {
			case TTY_PROC_NR:
				log_info(&log, "got tty proc nr message\n");
				break;
			case HARDWARE:
				uhci_interrupt();
				break;
			case CLOCK:
				uhci_root_intr();
				break;
			default:
				log_warn(&log, "illegal notify source: %d", m.m_source);
			}
			continue;
		}else{
			switch (m.m_type) {
//            	case USB_HCD_CTRL_REQ:
//                	handle_control_request_message(&usb_hcd, &m);
//                	break;
				default:
					log_info(&log, "received non ipc notify message (type=%d, source=%d", m.m_type, m.m_source);
			}
		}
	}
	log_info(&log, "stopping uhci...\n");
	return 0;
}

