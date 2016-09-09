#ifndef __ACPI_TABLE_H__
#define __ACPI_TABLE_H__

#include <minix/ipc.h>

void do_get_table(message *m);
void do_get_table_header(message *m);

#endif /* __ACPI_TABLE_H__ */
