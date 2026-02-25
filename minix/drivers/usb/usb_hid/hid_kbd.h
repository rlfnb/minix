/*
 * USB HID Boot Protocol keyboard handling
 *
 * USB HID keyboards in Boot Protocol mode send 8-byte reports:
 *   Byte 0: Modifier bitmap (Ctrl, Shift, Alt, GUI)
 *   Byte 1: Reserved
 *   Bytes 2-7: Up to 6 simultaneous key codes (HID Usage Page 0x07)
 *
 * The key codes are identical to MINIX's INPUT_KEY_* values in input.h,
 * so no translation table is needed.
 */

#ifndef _HID_KBD_H_
#define _HID_KBD_H_

#include <stdint.h>

#define HID_KBD_REPORT_SIZE     8
#define HID_KBD_MAX_KEYS        6

/* Boot protocol report structure */
struct hid_kbd_report {
	uint8_t modifiers;
	uint8_t reserved;
	uint8_t keys[HID_KBD_MAX_KEYS];
};

/* Modifier bit positions */
#define HID_MOD_LEFT_CTRL       0
#define HID_MOD_LEFT_SHIFT      1
#define HID_MOD_LEFT_ALT        2
#define HID_MOD_LEFT_GUI        3
#define HID_MOD_RIGHT_CTRL      4
#define HID_MOD_RIGHT_SHIFT     5
#define HID_MOD_RIGHT_ALT       6
#define HID_MOD_RIGHT_GUI       7

/* HID class-specific requests */
#define HID_SET_PROTOCOL        0x0B
#define HID_PROTOCOL_BOOT       0x00
#define HID_PROTOCOL_REPORT     0x01

#define HID_SET_REPORT          0x09
#define HID_REPORT_TYPE_OUTPUT  0x02

/* HID interface class codes */
#define USB_CLASS_HID           0x03
#define USB_SUBCLASS_BOOT       0x01
#define USB_PROTOCOL_KEYBOARD   0x01

/* Initialize keyboard state */
void hid_kbd_init(void);

/* Process a boot protocol report, generate input events */
void hid_kbd_process_report(const uint8_t *data, int len);

/* Set keyboard LEDs via SET_REPORT */
void hid_kbd_set_leds(unsigned int led_mask);

#endif /* !_HID_KBD_H_ */
