#include "pci.h"
#include "device.h"
#include "heap.h"
#include "log.h"
#include "memory.h"
#include "port_io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_VENDOR_NONE    0xFFFFU

static uint32_t pci_config_address(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
)
{
    return (1U << 31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)(device & 0x1FU) << 11) |
           ((uint32_t)(function & 0x07U) << 8) |
           ((uint32_t)offset & 0xFCU);
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read32(bus, device, function, offset);
    return (uint16_t)((value >> ((offset & 2U) * 8U)) & 0xFFFFU);
}

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read32(bus, device, function, offset);
    return (uint8_t)((value >> ((offset & 3U) * 8U)) & 0xFFU);
}

static void uint_to_hex_nibble(uint8_t nibble, char *out)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    *out = hex_digits[nibble & 0x0FU];
}

static void format_hex8(uint8_t value, char *buffer)
{
    uint_to_hex_nibble((uint8_t)(value >> 4), &buffer[0]);
    uint_to_hex_nibble(value, &buffer[1]);
    buffer[2] = '\0';
}

static void format_hex16(uint16_t value, char *buffer)
{
    uint_to_hex_nibble((uint8_t)(value >> 12), &buffer[0]);
    uint_to_hex_nibble((uint8_t)(value >> 8), &buffer[1]);
    uint_to_hex_nibble((uint8_t)(value >> 4), &buffer[2]);
    uint_to_hex_nibble((uint8_t)value, &buffer[3]);
    buffer[4] = '\0';
}

static const char *pci_class_name(uint8_t class_code)
{
    switch (class_code)
    {
        case 0x01:
            return "Mass Storage Controller";
        case 0x02:
            return "Network Controller";
        case 0x03:
            return "Display Controller";
        case 0x06:
            return "Bridge Device";
        case 0x0C:
            return "USB Controller";
        default:
            return NULL;
    }
}

static void pci_log_device(const device_t *dev)
{
    char line[80];
    char bus_hex[3];
    char device_hex[3];
    char vendor_hex[5];
    char device_id_hex[5];
    char class_hex[3];
    const char *class_name;
    uint32_t i;

    format_hex8(dev->bus, bus_hex);
    format_hex8(dev->device, device_hex);
    format_hex16(dev->vendor_id, vendor_hex);
    format_hex16(dev->device_id, device_id_hex);

    /* "BB:DD.F" — klog category already supplies PCI */
    i = 0;
    line[i++] = bus_hex[0];
    line[i++] = bus_hex[1];
    line[i++] = ':';
    line[i++] = device_hex[0];
    line[i++] = device_hex[1];
    line[i++] = '.';
    line[i++] = (char)('0' + (dev->function & 0x07U));
    line[i] = '\0';
    klog(KLOG_INFO, "PCI", line);

    i = 0;
    line[i++] = 'V';
    line[i++] = 'e';
    line[i++] = 'n';
    line[i++] = 'd';
    line[i++] = 'o';
    line[i++] = 'r';
    line[i++] = ':';
    line[i++] = ' ';
    line[i++] = vendor_hex[0];
    line[i++] = vendor_hex[1];
    line[i++] = vendor_hex[2];
    line[i++] = vendor_hex[3];
    line[i] = '\0';
    klog(KLOG_INFO, "PCI", line);

    i = 0;
    line[i++] = 'D';
    line[i++] = 'e';
    line[i++] = 'v';
    line[i++] = 'i';
    line[i++] = 'c';
    line[i++] = 'e';
    line[i++] = ':';
    line[i++] = ' ';
    line[i++] = device_id_hex[0];
    line[i++] = device_id_hex[1];
    line[i++] = device_id_hex[2];
    line[i++] = device_id_hex[3];
    line[i] = '\0';
    klog(KLOG_INFO, "PCI", line);

    class_name = pci_class_name(dev->class_code);
    i = 0;
    line[i++] = 'C';
    line[i++] = 'l';
    line[i++] = 'a';
    line[i++] = 's';
    line[i++] = 's';
    line[i++] = ':';
    line[i++] = ' ';

    if (class_name != NULL)
    {
        while (*class_name != '\0' && i < sizeof(line) - 1U)
        {
            line[i++] = *class_name++;
        }
    }
    else
    {
        format_hex8(dev->class_code, class_hex);
        line[i++] = '0';
        line[i++] = 'x';
        line[i++] = class_hex[0];
        line[i++] = class_hex[1];
    }

    line[i] = '\0';
    klog(KLOG_INFO, "PCI", line);
}

static void pci_build_name(char *name, uint8_t bus, uint8_t device, uint8_t function)
{
    char bus_hex[3];
    char device_hex[3];
    uint32_t i = 0;

    format_hex8(bus, bus_hex);
    format_hex8(device, device_hex);

    name[i++] = 'p';
    name[i++] = 'c';
    name[i++] = 'i';
    name[i++] = ':';
    name[i++] = bus_hex[0];
    name[i++] = bus_hex[1];
    name[i++] = ':';
    name[i++] = device_hex[0];
    name[i++] = device_hex[1];
    name[i++] = '.';
    name[i++] = (char)('0' + (function & 0x07U));
    name[i] = '\0';
}

static void pci_register_function(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    device_t *dev;
    char *name;

    vendor_id = pci_read16(bus, device, function, 0x00);
    if (vendor_id == PCI_VENDOR_NONE)
    {
        return;
    }

    device_id = pci_read16(bus, device, function, 0x02);
    prog_if = pci_read8(bus, device, function, 0x09);
    subclass = pci_read8(bus, device, function, 0x0A);
    class_code = pci_read8(bus, device, function, 0x0B);

    dev = (device_t *)kmalloc(sizeof(device_t));
    if (dev == NULL)
    {
        klog(KLOG_WARN, "PCI", "Failed to allocate device entry");
        return;
    }

    name = (char *)kmalloc(16);
    if (name == NULL)
    {
        klog(KLOG_WARN, "PCI", "Failed to allocate device name");
        return;
    }

    memset(dev, 0, sizeof(device_t));
    pci_build_name(name, bus, device, function);

    dev->name = name;
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->class_code = class_code;
    dev->subclass = subclass;
    dev->prog_if = prog_if;

    if (!device_register(dev))
    {
        klog(KLOG_WARN, "PCI", "Failed to register device");
        return;
    }

    pci_log_device(dev);
}

static void pci_scan_bus(void)
{
    uint16_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;

    for (bus = 0; bus < 256; bus++)
    {
        for (device = 0; device < 32; device++)
        {
            vendor_id = pci_read16((uint8_t)bus, device, 0, 0x00);
            if (vendor_id == PCI_VENDOR_NONE)
            {
                continue;
            }

            for (function = 0; function < 8; function++)
            {
                pci_register_function((uint8_t)bus, device, function);
            }
        }
    }
}

void pci_initialize(void)
{
    klog(KLOG_INFO, "PCI", "Scanning PCI bus");
    pci_scan_bus();
    klog(KLOG_INFO, "PCI", "PCI scan complete");
}
