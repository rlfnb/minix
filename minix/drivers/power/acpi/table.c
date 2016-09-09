#include <minix/driver.h>
#include <acpi.h>
#include <actbl.h>
#include <assert.h>
#include <minix/acpi.h>

#include "acpi_globals.h"



void do_get_table_header(message *m)
{
	struct acpi_get_table_req * req = (struct acpi_get_table_req *) m;
	ACPI_STATUS status;
	ACPI_TABLE_HEADER buffer;

	status = AcpiGetTableHeader(req->signature, req->instance, &buffer);

	if (ACPI_FAILURE(status)) {
		goto map_error;
	}
	((struct acpi_get_table_resp *) m)->length = sizeof(ACPI_TABLE_HEADER);
	memcpy(((struct acpi_get_table_resp *) m)->_pad, &buffer, sizeof(ACPI_TABLE_HEADER));

map_error:
	((struct acpi_get_table_resp *) m)->err = status;
}

void do_get_table(message *m)
{
	struct acpi_get_table_req * req = (struct acpi_get_table_req *) m;
	ACPI_STATUS status;
	int r;
	ACPI_TABLE_HEADER * buffer;
	
	buffer = (ACPI_TABLE_HEADER *) calloc(1, req->length);
	if ( buffer == NULL)
	{
		status = ENOMEM;
		goto map_error;
	}
	status = AcpiGetTable(req->signature, req->instance, &buffer);

	if (ACPI_FAILURE(status)) {
		goto map_error;
	}

	((struct acpi_get_table_resp *) m)->err = status;
	r = sys_safecopyto(m->m_source, req->grant, 0, (vir_bytes) buffer, req->length);
	if (r != OK)
	{
		status = r;
		goto map_error;
	}
map_error:
	free(buffer);
	((struct acpi_get_table_resp *) m)->err = status;
}

