/*
 * USB Host Controller Driver <-> usb_core IPC protocol definitions.
 *
 * This header defines the data structures and constants used for
 * communication between HCD processes (uhci_hcd, ehci_hcd, etc.)
 * and the central usb_core coordinator process.
 */

#ifndef _MINIX_USB_HCD_H
#define _MINIX_USB_HCD_H

#include <sys/types.h>

/*
 * HCD capability flags (reported during registration).
 */
#define USB_HCD_CAP_LS    0x01  /* Low-Speed (1.5 Mbit/s) */
#define USB_HCD_CAP_FS    0x02  /* Full-Speed (12 Mbit/s) */
#define USB_HCD_CAP_HS    0x04  /* High-Speed (480 Mbit/s) */

/*
 * USB device speed values.
 */
#define USB_SPEED_LOW     0
#define USB_SPEED_FULL    1
#define USB_SPEED_HIGH    2

/*
 * Port status flags (reported in USB_HCD_PORT_STATUS).
 */
#define USB_PORT_CONNECTED    0x01
#define USB_PORT_ENABLED      0x02
#define USB_PORT_SUSPENDED    0x04
#define USB_PORT_RESET        0x08
#define USB_PORT_POWER        0x10

/*
 * Maximum limits.
 */
#define USB_MAX_DEVICES       128
#define USB_MAX_HCDS          8
#define USB_MAX_DRIVERS       16
#define USB_MAX_ENDPOINTS     16

/*
 * DS label names for service discovery.
 */
#define USB_CORE_LABEL        "usb_core"

#endif /* _MINIX_USB_HCD_H */
