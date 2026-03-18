#ifndef MB_UTILS_H
#define MB_UTILS_H

#include "kernel/kernel.h"

/*
 * Console output mode (set during pre_init):
 *   0 = VGA text mode (default, BIOS boot)
 *   1 = serial only (UEFI boot without framebuffer)
 */
extern int direct_con_mode;

void direct_cls(void);
void direct_print(const char*);
void direct_print_char(char);
int direct_read_char(unsigned char*);

#endif
