# Plan: Nativer MINIX USB-Stack ohne DDEKit

## Ziel

Ersetzung des monolithischen DDEKit-basierten `usbd` durch einen nativen
MINIX USB-Stack mit separaten Prozessen. Durchstich: UHCI -> usb_core ->
usb_hid -> Tastatureingabe.

## Architektur-Uebersicht

```
                      input server (TTY)
                           ^
                           | INPUT_EVENT
                           |
                      +---------+
                      | usb_hid |  Keyboard class driver
                      +---------+  (libinputdriver + libusb)
                           ^
                           | USB_COMPLETE_URB / USB_ANNOUCE_DEV
                           |
    +----------+     +----------+     +----------+
    | uhci_hcd | <-> | usb_core | <-> | (future  |
    | (Prozess)|     | (Prozess)|     |  HCDs)   |
    +----------+     +----------+     +----------+
         |                |
     PCI / IRQ        devman
     I/O Ports     Service Discovery (DS)
```

Jeder Kasten = ein eigener MINIX-Prozess mit eigenem `sef_receive_status()`
Message-Loop. Kein DDEKit, kein User-Space-Threading.


## Verzeichnisstruktur

```
minix/drivers/usb/
  usb_core/               # NEU - Zentraler USB-Koordinator
    usb_core.c            #   Main, SEF, Message-Loop
    device.c              #   Geraete-Registry, Enumeration
    device.h              #   Interne Typen
    urb.c                 #   URB-Routing zwischen HCD und Class-Driver
    urb.h
    Makefile
    usb_core.conf

  uhci_hcd/               # NEU - Standalone UHCI-Treiber
    uhci_hcd.c            #   Main, SEF, Message-Loop, PCI-Init
    uhci_hw.c             #   Hardware-Operationen (aus uhci_core.c)
    uhci_hw.h             #   Register + Datenstrukturen (aus uhci_core.h)
    uhci_regs.h           #   Register-Definitionen (wiederverwendet)
    Makefile
    uhci_hcd.conf

  usb_hid/                # NEU - HID Keyboard-Treiber
    usb_hid.c             #   Main, SEF, inputdriver + USB Message-Loop
    hid_kbd.c             #   Boot Protocol Parsing, Key-Events
    hid_kbd.h
    Makefile
    usb_hid.conf

minix/include/minix/
    usb.h                 #   Erweitert: neue HCD<->Core Message-Typen
    usb_hcd.h             #   NEU: HCD-Registrierung, Capabilities

minix/lib/libusb/
    usb.c                 #   Angepasst: "usb_core" statt "usbd" Lookup
```


## Phase 1: IPC-Protokoll definieren

### 1.1 Neue Nachrichten-Typen (in minix/include/minix/usb.h)

Bestehende Class-Driver <-> Core Nachrichten bleiben weitgehend erhalten:
```c
/* Bestehend (Class Driver <-> Core) */
USB_RQ_INIT           /* Driver registriert sich */
USB_RQ_DEINIT         /* Driver meldet sich ab */
USB_RQ_SEND_URB       /* URB einreichen */
USB_RQ_CANCEL_URB     /* URB abbrechen */
USB_REPLY             /* Antwort */
USB_COMPLETE_URB      /* URB abgeschlossen (async) */
USB_ANNOUCE_DEV       /* Geraet gefunden (async) */
USB_WITHDRAW_DEV      /* Geraet entfernt (async) */

/* NEU (HCD <-> Core) */
USB_HCD_REGISTER      /* HCD meldet sich bei usb_core */
USB_HCD_REGISTER_REPLY
USB_HCD_PORT_STATUS   /* Port-Status-Aenderung (connect/disconnect/reset) */
USB_HCD_SUBMIT_URB    /* usb_core -> HCD: fuehre diesen URB aus */
USB_HCD_URB_COMPLETE  /* HCD -> usb_core: URB fertig */
USB_HCD_RESET_PORT    /* usb_core -> HCD: Reset Port N */
USB_HCD_PORT_RESET_DONE /* HCD -> usb_core: Reset abgeschlossen, Speed */
```

### 1.2 Neuer Header: minix/include/minix/usb_hcd.h

```c
/* HCD-Faehigkeiten */
#define USB_HCD_CAP_LS    0x01  /* Low-Speed */
#define USB_HCD_CAP_FS    0x02  /* Full-Speed */
#define USB_HCD_CAP_HS    0x04  /* High-Speed */
#define USB_HCD_CAP_SS    0x08  /* SuperSpeed */

/* HCD-Registrierung */
struct usb_hcd_info {
    int num_ports;           /* Anzahl Root-Hub-Ports */
    unsigned int caps;       /* Geschwindigkeits-Flags */
    char name[16];           /* z.B. "uhci0" */
};

/* Port-Status */
#define USB_PORT_CONNECTED    0x01
#define USB_PORT_ENABLED      0x02
#define USB_PORT_LOW_SPEED    0x04
#define USB_PORT_FULL_SPEED   0x08
#define USB_PORT_HIGH_SPEED   0x10
#define USB_PORT_RESET        0x20
```

### 1.3 Safecopy-Grant-Modell fuer URBs

```
Class Driver                    usb_core                      HCD
    |                               |                          |
    |-- USB_RQ_SEND_URB ----------->|                          |
    |   (grant_id fuer URB-Daten)   |                          |
    |<-- USB_REPLY (urb_id) --------|                          |
    |                               |-- USB_HCD_SUBMIT_URB --->|
    |                               |   (neuer grant fuer      |
    |                               |    HCD-Zugriff)          |
    |                               |                          |
    |                               |<-- USB_HCD_URB_COMPLETE -|
    |                               |                          |
    |<-- USB_COMPLETE_URB ----------|                          |
    |   (Daten zurueck via Grant)   |                          |
```

usb_core kopiert URB-Daten einmal vom Class-Driver in eigenen Puffer,
erstellt neuen Grant fuer den HCD. Bei Completion: Rueckkopie.


## Phase 2: usb_core - Nativer USB-Koordinator

### 2.1 Main-Loop (usb_core.c)

```c
int main(void) {
    sef_startup();          /* SEF-Init */
    devman_init();          /* devman fuer Geraetebaum */

    /* Hauptschleife - empfaengt ALLE Nachrichten */
    while (running) {
        sef_receive_status(ANY, &msg, &ipc_status);

        if (is_ipc_notify(ipc_status)) {
            handle_notification(&msg);
            continue;
        }

        switch (msg.m_type) {
        /* Von HCDs */
        case USB_HCD_REGISTER:      hcd_register(&msg);      break;
        case USB_HCD_PORT_STATUS:   hcd_port_status(&msg);   break;
        case USB_HCD_URB_COMPLETE:  hcd_urb_complete(&msg);  break;
        case USB_HCD_PORT_RESET_DONE: hcd_reset_done(&msg);  break;

        /* Von Class-Drivers */
        case USB_RQ_INIT:           drv_register(&msg);      break;
        case USB_RQ_DEINIT:         drv_deregister(&msg);    break;
        case USB_RQ_SEND_URB:       drv_submit_urb(&msg);    break;
        case USB_RQ_CANCEL_URB:     drv_cancel_urb(&msg);    break;
        }
    }
}
```

Kein Threading noetig. Ein einziger `sef_receive_status(ANY)` Loop
handhabt alles sequentiell.

### 2.2 Geraete-Registry (device.c)

```c
#define USB_MAX_DEVICES   128
#define USB_MAX_HCDS      8
#define USB_MAX_DRIVERS   32

struct usb_hcd_entry {
    endpoint_t ep;              /* IPC-Endpoint des HCD */
    int num_ports;
    unsigned int caps;
    int active;
};

struct usb_device_entry {
    int hcd_index;              /* Welcher HCD besitzt das Geraet */
    int port;                   /* An welchem Port */
    int address;                /* USB-Adresse (1-127) */
    int speed;                  /* LOW/FULL/HIGH */
    usb_device_descriptor_t desc;
    int configured;
    int driver_ep;              /* Gebundener Class-Driver */
};

static struct usb_hcd_entry hcds[USB_MAX_HCDS];
static struct usb_device_entry devices[USB_MAX_DEVICES];
```

### 2.3 Geraete-Enumeration (device.c)

Wenn ein HCD eine Port-Verbindung meldet:

1. usb_core sendet USB_HCD_RESET_PORT an HCD
2. HCD fuehrt Hardware-Reset durch, meldet Speed zurueck
3. usb_core fuehrt Standard-Enumeration per Control-Transfers durch:
   - GET_DESCRIPTOR (Device) an Adresse 0, max_packet_size ermitteln
   - SET_ADDRESS (freie Adresse zuweisen)
   - GET_DESCRIPTOR (Device, Config, Interface, Endpoint) vollstaendig
   - SET_CONFIGURATION
4. usb_core registriert Geraet bei devman
5. usb_core sendet USB_ANNOUCE_DEV an passenden Class-Driver

Control-Transfers laufen ueber dasselbe URB-Routing wie alle anderen
Transfers - usb_core reicht sie als USB_HCD_SUBMIT_URB an den HCD weiter.

### 2.4 system.conf

```
service usb_core {
    system PRIVCTL UMAP;
    ipc SYSTEM pm rs log tty ds vfs vm devman
        uhci_hcd ehci_hcd xhci_hcd      # HCDs
        usb_hid usb_storage usb_hub;     # Class Drivers
    uid 0;
};
```


## Phase 3: uhci_hcd - Standalone UHCI-Prozess

### 3.1 Was wiederverwendet wird

Aus dem bestehenden UHCI-Code:
- uhci_regs.h     -> 1:1 uebernommen (Register-Definitionen)
- uhci_core.c/h   -> Basis fuer uhci_hw.c/h (Hardware-Logik)
  - Frame-List, TD/QH-Management
  - Transfer-Ausfuehrung (setup/data/status stages)
  - Interrupt-Handling

### 3.2 Was sich aendert

Statt DDEKit-Callbacks und hcd_driver_state-Funktionszeiger:

```c
/* uhci_hcd.c - Main */
int main(void) {
    sef_startup();
    pci_init();                    /* PCI-Bus scannen */
    uhci_hw_init();                /* UHCI Hardware initialisieren */
    register_with_usb_core();     /* Bei usb_core anmelden */
    uhci_poll_ports_initial();     /* Initiale Port-Erkennung */

    while (running) {
        sef_receive_status(ANY, &msg, &ipc_status);

        if (is_ipc_notify(ipc_status)) {
            if (_ENDPOINT_P(msg.m_source) == HARDWARE)
                uhci_interrupt_handler();  /* IRQ verarbeiten */
            else if (_ENDPOINT_P(msg.m_source) == CLOCK)
                uhci_timer_handler();      /* Port-Polling */
            continue;
        }

        switch (msg.m_type) {
        case USB_HCD_SUBMIT_URB:     uhci_submit_urb(&msg);    break;
        case USB_HCD_RESET_PORT:     uhci_reset_port(&msg);    break;
        }
    }
}
```

### 3.3 IRQ-Handling nativ

```c
static int irq_hook_id;

static void uhci_setup_irq(int irq) {
    irq_hook_id = irq;
    sys_irqsetpolicy(irq, IRQ_REENABLE, &irq_hook_id);
    sys_irqenable(&irq_hook_id);
}

/* Wird aus Main-Loop bei HARDWARE-Notification aufgerufen */
static void uhci_interrupt_handler(void) {
    hcd_reg2 status = uhci_read_reg16(UHCI_USBSTS);

    if (status & UHCI_USBSTS_USBINT) {
        /* Transfer abgeschlossen - abgeschlossene TDs finden */
        uhci_scan_completed_tds();
    }
    if (status & UHCI_USBSTS_ERROR) {
        uhci_handle_error();
    }
    if (status & UHCI_USBSTS_RD) {
        /* Resume Detect */
    }

    /* Status-Bits quittieren */
    uhci_write_reg16(UHCI_USBSTS, status);
    sys_irqenable(&irq_hook_id);  /* IRQ wieder aktivieren */
}
```

### 3.4 Registrierung bei usb_core

```c
static void register_with_usb_core(void) {
    endpoint_t core_ep;
    message msg;

    /* usb_core ueber DS finden */
    ds_retrieve_label_endpt("usb_core", &core_ep);

    /* Registrierungsnachricht */
    msg.m_type = USB_HCD_REGISTER;
    msg.USB_HCD_PORTS = num_ports;   /* Typisch 2 fuer UHCI */
    msg.USB_HCD_CAPS = USB_HCD_CAP_LS | USB_HCD_CAP_FS;

    ipc_sendrec(core_ep, &msg);
    /* msg.m_type == USB_HCD_REGISTER_REPLY */
}
```

### 3.5 system.conf

```
service uhci_hcd {
    system PRIVCTL UMAP IRQCTL DEVIO;
    pci device 0c/03/00;
    ipc SYSTEM pm rs log tty ds vm pci usb_core;
    uid 0;
};
```


## Phase 4: usb_hid - Keyboard-Treiber

### 4.1 Architektur

Der USB HID Keyboard-Treiber verbindet zwei Frameworks:
- **libinputdriver** fuer Keyboard-Events an den Input-Server
- **libusb** (angepasst) fuer USB-Kommunikation mit usb_core

```c
/* usb_hid.c */
static struct inputdriver usb_hid_tab = {
    .idr_leds   = usb_hid_set_leds,    /* LED-Steuerung via SET_REPORT */
    .idr_intr   = NULL,                 /* Keine HW-IRQs */
    .idr_alarm  = usb_hid_alarm,        /* Timer fuer Polling/Retry */
    .idr_other  = usb_hid_other,        /* USB-Nachrichten hier */
};

static struct usb_driver usb_hid_driver = {
    .connect_device    = usb_hid_connect,
    .disconnect_device = usb_hid_disconnect,
    .urb_completion    = usb_hid_urb_complete,
};

int main(void) {
    sef_startup();
    usb_init("usb_hid");               /* Bei usb_core registrieren */
    inputdriver_announce(INPUT_DEV_KBD); /* Beim Input-Server melden */
    inputdriver_task(&usb_hid_tab);      /* Main-Loop */
    return 0;
}

/* USB-Nachrichten kommen ueber idr_other */
static void usb_hid_other(message *m, int ipc_status) {
    usb_handle_msg(&usb_hid_driver, m);
}
```

### 4.2 USB HID Boot Protocol (hid_kbd.c)

USB-Tastaturen im Boot Protocol senden 8-Byte Reports:

```
Byte 0: Modifier-Bits (Ctrl, Shift, Alt, GUI - links/rechts)
Byte 1: Reserved (0x00)
Byte 2-7: Aktuell gedrueckte Tasten (max 6, USB HID Usage Codes)
```

```c
struct hid_kbd_report {
    uint8_t modifiers;    /* Bit 0=LCtrl,1=LShift,2=LAlt,3=LGUI,
                                 4=RCtrl,5=RShift,6=RAlt,7=RGUI */
    uint8_t reserved;
    uint8_t keys[6];      /* Bis zu 6 gleichzeitige Tasten */
};

/* Modifier-Bit zu HID Usage Code Mapping */
static const uint16_t mod_to_usage[8] = {
    INPUT_KEY_LEFT_CTRL,  INPUT_KEY_LEFT_SHIFT,
    INPUT_KEY_LEFT_ALT,   INPUT_KEY_LEFT_GUI,
    INPUT_KEY_RIGHT_CTRL, INPUT_KEY_RIGHT_SHIFT,
    INPUT_KEY_RIGHT_ALT,  INPUT_KEY_RIGHT_GUI
};

static struct hid_kbd_report prev_report;  /* Vorheriger Report */

static void hid_kbd_process_report(uint8_t *data, int len) {
    struct hid_kbd_report *report = (struct hid_kbd_report *)data;
    int i, j, found;

    /* Modifier-Aenderungen erkennen */
    uint8_t mod_diff = report->modifiers ^ prev_report.modifiers;
    for (i = 0; i < 8; i++) {
        if (mod_diff & (1 << i)) {
            inputdriver_send_event(FALSE, INPUT_PAGE_KEY,
                mod_to_usage[i],
                (report->modifiers & (1 << i))
                    ? INPUT_PRESS : INPUT_RELEASE, 0);
        }
    }

    /* Losgelassene Tasten: in prev aber nicht in current */
    for (i = 0; i < 6; i++) {
        if (prev_report.keys[i] == 0) continue;
        found = 0;
        for (j = 0; j < 6; j++) {
            if (prev_report.keys[i] == report->keys[j]) {
                found = 1; break;
            }
        }
        if (!found) {
            inputdriver_send_event(FALSE, INPUT_PAGE_KEY,
                prev_report.keys[i], INPUT_RELEASE, 0);
        }
    }

    /* Neu gedrueckte Tasten: in current aber nicht in prev */
    for (i = 0; i < 6; i++) {
        if (report->keys[i] == 0) continue;
        found = 0;
        for (j = 0; j < 6; j++) {
            if (report->keys[i] == prev_report.keys[j]) {
                found = 1; break;
            }
        }
        if (!found) {
            inputdriver_send_event(FALSE, INPUT_PAGE_KEY,
                report->keys[i], INPUT_PRESS, 0);
        }
    }

    prev_report = *report;
}
```

### 4.3 Interrupt-Transfer-Polling

```c
/* Nach Device-Connect: Interrupt-Endpoint finden und Polling starten */
static void usb_hid_connect(int dev_id, unsigned int interfaces) {
    /* SET_PROTOCOL(0) = Boot Protocol */
    usb_hid_set_boot_protocol(dev_id);

    /* Interrupt IN URB einreichen (8 Bytes, Endpoint 1, Interval) */
    usb_hid_submit_interrupt_in(dev_id);
}

/* URB-Completion: Report verarbeiten, naechsten URB einreichen */
static void usb_hid_urb_complete(struct usb_urb *urb) {
    if (urb->status == 0 && urb->actual_length >= 8) {
        hid_kbd_process_report(urb->buffer, urb->actual_length);
    }

    /* Sofort naechsten Interrupt-Transfer einreichen (Polling) */
    usb_hid_submit_interrupt_in(current_dev_id);
}
```

### 4.4 system.conf

```
service usb_hid {
    system PRIVCTL UMAP;
    ipc SYSTEM pm rs log tty ds vfs vm usb_core input;
    uid 0;
};
```


## Phase 5: libusb anpassen

### 5.1 Aenderung in libusb/usb.c

```c
/* Alt: */
res = ds_retrieve_label_endpt("usbd", &hcd_ep);

/* Neu: */
res = ds_retrieve_label_endpt("usb_core", &hcd_ep);
```

Das IPC-Protokoll zwischen Class-Driver und usb_core bleibt identisch
zum bestehenden Protokoll. libusb funktioniert fast unveraendert.


## Phase 6: Aufraumen

### 6.1 Entfernen

- minix/drivers/usb/usbd/          -> komplett entfernen
- DDEKit-Abhaengigkeiten in libusb -> durch native IPC ersetzen
- DDEKit USB-spezifischer Code     -> nicht mehr noetig

### 6.2 Behalten

- minix/lib/libddekit/             -> andere Treiber koennten es nutzen
  (aktuell nur USB, kann spaeter entfernt werden)
- minix/include/minix/usb.h        -> erweitert, nicht ersetzt
- minix/include/minix/usb_ch9.h    -> USB-Deskriptor-Typen unveraendert
- minix/lib/libusb/                -> angepasst (Label-Name)
- minix/lib/libinputdriver/        -> unveraendert


## Implementierungsreihenfolge

```
Schritt  Komponente       Testbar als
------   ----------       -----------
  1      IPC-Header       Kompiliert ohne Fehler
  2      usb_core Stub    Startet, registriert sich bei DS
  3      uhci_hcd         Findet PCI-Geraet, registriert bei usb_core
  4      usb_core Enum    Erkennt USB-Geraet, liest Deskriptoren
  5      usb_hid Stub     Registriert sich, empfaengt ANNOUCE_DEV
  6      usb_hid Report   Liest Boot-Protocol-Reports
  7      usb_hid Input    Tastendruecke erscheinen in TTY
  8      Aufraumen        Alten usbd-Code entfernen
```

Jeder Schritt ist einzeln testbar und liefert sichtbaren Fortschritt.


## Risiken und Abhaengigkeiten

1. **Double-Copy Overhead**: URB-Daten werden Class-Driver -> usb_core ->
   HCD kopiert. Fuer Low/Full-Speed (UHCI, max 8/64 Bytes pro Paket)
   vernachlaessigbar. Fuer High-Speed (EHCI) spaeter Grant-Forwarding
   evaluieren.

2. **Timing**: USB-Enumeration hat Zeitvorgaben (z.B. 50ms nach Reset).
   Da alles ueber IPC laeuft, muss die Latenz klein genug sein.
   Unkritisch fuer Keyboard-Durchstich (keine harten Timing-Anforderungen
   ausser Port-Reset, der im HCD bleibt).

3. **Boot-Reihenfolge**: usb_core muss vor uhci_hcd starten, uhci_hcd
   vor usb_hid. Ueber RS-Abhaengigkeiten oder DS-Polling loesbar.

4. **Grant-Kette**: usb_core muss Grants zwischen Class-Driver und HCD
   vermitteln. Entweder Re-Grant (effizient) oder Copy (einfach).
   Start mit Copy, spaeter optimieren.
