#include <sys/types.h>
#include <minix/ipc.h>

#define ACPI_REQ_GET_IRQ			1
#define ACPI_REQ_MAP_BRIDGE			2
#define ACPI_REQ_GET_TABLE_HEADER		3
#define ACPI_REQ_GET_TABLE			4

struct acpi_request_hdr {
	endpoint_t 	m_source; /* message header */
	u32_t		request;
};

/*
 * Message to request dev/pin translation to IRQ by acpi using the acpi routing
 * tables
 */
struct acpi_get_irq_req {
	struct acpi_request_hdr	hdr;
	u32_t			bus;
	u32_t			dev;
	u32_t			pin;
	u32_t			__padding[4];
};

/* response from acpi to acpi_get_irq_req */
struct acpi_get_irq_resp {
	endpoint_t 	m_source; /* message header */
	i32_t		irq;
	u32_t		__padding[7];
};

/* message format for pci bridge mappings to acpi */
struct acpi_map_bridge_req {
	struct acpi_request_hdr	hdr;
	u32_t	primary_bus;
	u32_t	secondary_bus;
	u32_t	device;
	u32_t	__padding[4];
};

struct acpi_map_bridge_resp {
	endpoint_t 	m_source; /* message header */
	int		err;
	u32_t		__padding[7];
};

struct acpi_get_table_req {
	struct acpi_request_hdr hdr;
	char	signature[4];
	u32_t 	instance;
    size_t	length;
    cp_grant_id_t grant;
};

struct acpi_get_table_resp {
	endpoint_t		m_source; /* message header */
	int				err;
	size_t 			length;
	u8_t			_pad[40];
};

int acpi_init(void);
int acpi_get_irq(unsigned bus, unsigned dev, unsigned pin);
int acpi_get_table_header(char signature[4], u32_t instance, vir_bytes * buffer);
int acpi_get_table(char signature[4], u32_t instance, vir_bytes * buffer, size_t length);
void acpi_map_bridge(unsigned int pbnr, unsigned int dev, unsigned int sbnr);

