#define _SYSTEM

#include <errno.h>
#include <string.h>

#include <minix/acpi.h>
#include <minix/com.h>
#include <minix/ds.h>
#include <minix/ipc.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

static struct log log =
{ .name = "libacpi", .log_level = LEVEL_WARN, .log_func = default_log };

static endpoint_t acpi_ep = NONE;

int
acpi_init(void)
{
	int res;
	res = ds_retrieve_label_endpt("acpi", &acpi_ep);
	return res;
}

int
acpi_get_table_header(char signature[4], u32_t instance, vir_bytes * buffer)
{
	struct acpi_get_table_req * req;
	struct acpi_get_table_resp * resp;
	int err;
	message m;

	if(acpi_ep == NONE)
	{
		err = acpi_init();
		if (OK != err)
		{
			log_warn(&log, "cannot resolve acpi endpoint: %d\n", err);
			return err;
		}
	}
	req = (struct acpi_get_table_req *)&m;
	req->hdr.request = ACPI_REQ_GET_TABLE_HEADER;
	memcpy(req->signature, signature, 4);
	req->instance = instance;

	log_trace(&log, "send message: signature=%s instance=%d\n", req->signature, req->instance);

	if ((err = ipc_sendrec(acpi_ep, &m)) != OK)
	{
		log_warn(&log, "error %d while receiveing from ACPI\n", err);
		return err;
	}

	resp = (struct acpi_get_table_resp *)&m;
	if (resp->err == 0 ) 
	{
		memcpy(buffer, resp->_pad , resp->length);
	} else {
		log_info(&log, "acpi_get_table_header returned non sucessful status: %d\n", resp->err);
	}

	return resp->err;
}

int
acpi_get_table(char signature[4], u32_t instance, vir_bytes * buffer, size_t length)
{
	struct acpi_get_table_req * req;
	int err;
	message m;
	cp_grant_id_t grant;

	if(acpi_ep == NONE)
	{
		err = acpi_init();
		if (OK != err)
		{
			log_warn(&log, "cannot resolve acpi endpoint: %d\n", err);
			return err;
		}
	}
	grant = cpf_grant_direct(acpi_ep, (vir_bytes) buffer, length, CPF_WRITE);
	if ( grant == -1)
	{
		log_warn(&log, "grant not created\n");
		return -1;	
	}
	req = (struct acpi_get_table_req *)&m;
	req->hdr.request = ACPI_REQ_GET_TABLE;
	memcpy(req->signature, signature, 4);
	req->instance = instance;
	req->length = length;
	req->grant = grant;
	if ((err = ipc_sendrec(acpi_ep, &m)) != OK)
	{
		log_warn(&log, "error %d while receiveing from ACPI\n", err);
		return err;
	}
	cpf_revoke(grant);
	return ((struct acpi_get_table_resp *)&m)->err;
}

/*===========================================================================*
 *				IRQ handling				     *
 *===========================================================================*/
int
acpi_get_irq(unsigned bus, unsigned dev, unsigned pin)
{
	int err;
	message m;

	if (acpi_ep == NONE) {
		err = acpi_init();
		if (OK != err) {
			panic("libacpi: ds_retrieve_label_endpt failed for 'acpi': %d", err);
		}
		else {
			log_info(&log, "resolved acpi to endpoint: %d\n", acpi_ep);
		}
	}

	((struct acpi_get_irq_req *)&m)->hdr.request = ACPI_REQ_GET_IRQ;
	((struct acpi_get_irq_req *)&m)->bus = bus;
	((struct acpi_get_irq_req *)&m)->dev = dev;
	((struct acpi_get_irq_req *)&m)->pin = pin;

	if ((err = ipc_sendrec(acpi_ep, &m)) != OK)
		panic("libacpi: error %d while receiving from ACPI\n", err);

	return ((struct acpi_get_irq_resp *)&m)->irq;
}

/*
 * tells acpi which two busses are connected by this bridge. The primary bus
 * (pbnr) must be already known to acpi and it must map dev as the connection to
 * the secondary (sbnr) bus
 */
void
acpi_map_bridge(unsigned int pbnr, unsigned int dev, unsigned int sbnr)
{
	int err;
	message m;

	if (acpi_ep == NONE) {
		err = acpi_init();
		if (OK != err) {
			panic("libacpi: ds_retrieve_label_endpt failed for 'acpi': %d", err);
		}
	}

	((struct acpi_map_bridge_req *)&m)->hdr.request = ACPI_REQ_MAP_BRIDGE;
	((struct acpi_map_bridge_req *)&m)->primary_bus = pbnr;
	((struct acpi_map_bridge_req *)&m)->secondary_bus = sbnr;
	((struct acpi_map_bridge_req *)&m)->device = dev;

	if ((err = ipc_sendrec(acpi_ep, &m)) != OK)
		panic("libacpi: error %d while receiving from ACPI\n", err);

	if (((struct acpi_map_bridge_resp *)&m)->err != OK)
		printf("libacpi: acpi failed to map pci (%d) to pci (%d) bridge\n",
								pbnr, sbnr);
}
