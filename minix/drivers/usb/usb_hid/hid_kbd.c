/*
 * USB HID Boot Protocol keyboard processing
 *
 * Parses 8-byte boot protocol reports and generates MINIX input events.
 * The USB HID Usage Page 0x07 key codes map 1:1 to MINIX's INPUT_KEY_*
 * values, so we can pass them directly to inputdriver_send_event().
 */

#include <minix/input.h>
#include <minix/inputdriver.h>
#include <string.h>

#include "hid_kbd.h"

/* Previous report for detecting changes */
static struct hid_kbd_report prev_report;

/* LED state to send via SET_REPORT (output report byte) */
static uint8_t led_output_report;

/* Modifier bit -> HID Usage Code mapping.
 * These match INPUT_KEY_LEFT_CTRL..INPUT_KEY_RIGHT_GUI (0xE0-0xE7). */
static const uint16_t mod_to_usage[8] = {
	INPUT_KEY_LEFT_CTRL,
	INPUT_KEY_LEFT_SHIFT,
	INPUT_KEY_LEFT_ALT,
	INPUT_KEY_LEFT_GUI,
	INPUT_KEY_RIGHT_CTRL,
	INPUT_KEY_RIGHT_SHIFT,
	INPUT_KEY_RIGHT_ALT,
	INPUT_KEY_RIGHT_GUI
};


/*===========================================================================*
 *    hid_kbd_init                                                           *
 *===========================================================================*/
void
hid_kbd_init(void)
{
	memset(&prev_report, 0, sizeof(prev_report));
	led_output_report = 0;
}


/*===========================================================================*
 *    hid_kbd_process_report                                                 *
 *===========================================================================*/
void
hid_kbd_process_report(const uint8_t *data, int len)
{
	const struct hid_kbd_report *report;
	uint8_t mod_diff;
	int i, j, found;

	if (len < HID_KBD_REPORT_SIZE)
		return;

	report = (const struct hid_kbd_report *)data;

	/* Check for rollover error (all keys = 0x01) */
	if (report->keys[0] == 0x01)
		return;

	/* Detect modifier changes */
	mod_diff = report->modifiers ^ prev_report.modifiers;
	for (i = 0; i < 8; i++) {
		if (mod_diff & (1 << i)) {
			inputdriver_send_event(0 /*keyboard*/,
				INPUT_PAGE_KEY,
				mod_to_usage[i],
				(report->modifiers & (1 << i)) ?
					INPUT_PRESS : INPUT_RELEASE,
				0);
		}
	}

	/* Detect released keys: in prev_report but not in current */
	for (i = 0; i < HID_KBD_MAX_KEYS; i++) {
		if (prev_report.keys[i] == 0)
			continue;

		found = 0;
		for (j = 0; j < HID_KBD_MAX_KEYS; j++) {
			if (prev_report.keys[i] == report->keys[j]) {
				found = 1;
				break;
			}
		}

		if (!found) {
			inputdriver_send_event(0 /*keyboard*/,
				INPUT_PAGE_KEY,
				prev_report.keys[i],
				INPUT_RELEASE,
				0);
		}
	}

	/* Detect newly pressed keys: in current but not in prev_report */
	for (i = 0; i < HID_KBD_MAX_KEYS; i++) {
		if (report->keys[i] == 0)
			continue;

		found = 0;
		for (j = 0; j < HID_KBD_MAX_KEYS; j++) {
			if (report->keys[i] == prev_report.keys[j]) {
				found = 1;
				break;
			}
		}

		if (!found) {
			inputdriver_send_event(0 /*keyboard*/,
				INPUT_PAGE_KEY,
				report->keys[i],
				INPUT_PRESS,
				0);
		}
	}

	/* Save current report for next comparison */
	memcpy(&prev_report, report, sizeof(prev_report));
}


/*===========================================================================*
 *    hid_kbd_set_leds                                                       *
 *===========================================================================*/
void
hid_kbd_set_leds(unsigned int led_mask)
{
	/* Convert MINIX LED flags to HID output report byte.
	 * HID LED Usage IDs: NumLock=1, CapsLock=2, ScrollLock=3
	 * These map to bits 0, 1, 2 in the output report. */
	led_output_report = 0;

	if (led_mask & (1 << INPUT_LED_NUMLOCK))
		led_output_report |= 0x01;
	if (led_mask & (1 << INPUT_LED_CAPSLOCK))
		led_output_report |= 0x02;
	if (led_mask & (1 << INPUT_LED_SCROLLLOCK))
		led_output_report |= 0x04;

	/* The actual SET_REPORT transfer is handled by usb_hid.c
	 * which calls usb_send_urb() with the output report.
	 * For now we just store the desired LED state. */
}
