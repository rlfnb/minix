#include <sys/types.h>

#include <minix/chardriver.h>
#include <minix/driver.h>
#include <minix/rs.h>
#include <minix/log.h>
#include <machine/pci.h>

#include <sys/mman.h>

#include "smbus.h"

static struct log log = { .name = "smbus", .log_level = LEVEL_INFO, .log_func = default_log };

int sef_cb_init(int type, sef_init_info_t *info); 
int smbus_probe(void);
int init_controller(struct smbus_hc *controller);

int init_controller(struct smbus_hc *controller) {
    int r, ioflag;

    pci_reserve(controller->devind);

    u16_t pci_cmd = pci_attr_r16(controller->devind, PCI_CR);
    pci_cmd |= PCI_CR_MAST_EN | PCI_CR_MEM_EN;
    pci_attr_w16(controller->devind, PCI_CR, pci_cmd);

    r = pci_get_bar(controller->devind, PCI_BAR, &controller->base, &controller->size, &ioflag);
    if (r != OK)
        panic("could not get base address register!");

    controller->irq = pci_attr_r8(controller->devind, PCI_ILR);

    if ((r = sys_irqsetpolicy(controller->irq, 0, &controller->irq_hook)) != OK)
        panic("could not set irq policy!");
    controller->regs = vm_map_phys(SELF, (void *) controller->base, controller->size);

    if (controller->regs == MAP_FAILED)
        panic("Could NOT do vm_map_phys!");

    return OK;
}

int smbus_probe() {
    int devind;
    u16_t r, vid, did;
    r = 0;
    devind = 0;
    vid = 0;
    did = 0;

    pci_init();
    r = pci_first_dev(&devind, &vid, &did);
    log_trace(&log, "first device (%x:%x %d) return code: %d\n", vid, did, devind, r);
    if (r == 1) {
        printf("Starting SMBus (vendor: %x device: %x)\n", vid, did);
    } else {
        log_warn(&log, "could not find device!\n");
    return ENXIO;
}
return OK;
}


/*===========================================================================*
 *                              sef_cb_init_fresh                            *
 *===========================================================================*/
int
sef_cb_init(int type, sef_init_info_t *info)
{
        /* Initialize the driver. */
        int do_announce_driver = -1;

        switch(type) {
        case SEF_INIT_FRESH:
        case SEF_INIT_RESTART:
                do_announce_driver = TRUE;
                break;
        case SEF_INIT_LU:
                do_announce_driver = FALSE;
                break;
        default:
                panic("Unknown type of restart");
                break;
        }

        /* Announce we are up when necessary. */
        if (TRUE == do_announce_driver) {
//                chardriver_announce();
        }

        /* Initialization completed successfully. */
        return OK;
}
/*======================================================================*
 *                      SEF Callbacks                                   *
 *======================================================================*/
static void
sef_local_startup(void)
{
        /*
         * Register init callbacks. Use the same function for all event types
         */
        sef_setcb_init_fresh(sef_cb_init);
        sef_setcb_init_restart(sef_cb_init);

        /* Let SEF perform startup. */
        sef_startup();
}

/*======================================================================*
 *                              main                                    *
 *======================================================================*/
int
main(void)
{
	int r = smbus_probe();
	if(r != OK)
		return r; 

        /*
         * Perform initialization.
         */
        sef_local_startup();

        /*
         * Run the main loop.
         */
//        chardriver_task(&driver);
        return OK;
}

