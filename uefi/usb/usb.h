#ifndef TESTOS_UEFI_USB_USB_H
#define TESTOS_UEFI_USB_USB_H

#include "types.h"

#define USB_REQ_GET_DESCRIPTOR 0x06U
#define USB_REQ_SET_CONFIGURATION 0x09U
#define USB_REQ_SET_INTERFACE 0x0BU
#define USB_REQ_SET_PROTOCOL 0x0BU
#define USB_REQ_HID_SET_PROTOCOL 0x0BU

#define USB_DESC_DEVICE        0x01U
#define USB_DESC_CONFIGURATION 0x02U
#define USB_DESC_INTERFACE     0x04U
#define USB_DESC_ENDPOINT      0x05U
#define USB_DESC_HID           0x21U

#define USB_CLASS_HID          0x03U
#define USB_SUBCLASS_BOOT      0x01U
#define USB_PROTO_KEYBOARD     0x01U
#define USB_PROTO_MOUSE        0x02U

typedef struct usb_device_descriptor
{
    uint8_t length;
    uint8_t type;
    uint16_t usb_version;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size0;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t manufacturer;
    uint8_t product;
    uint8_t serial;
    uint8_t num_configurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct usb_config_descriptor
{
    uint8_t length;
    uint8_t type;
    uint16_t total_length;
    uint8_t num_interfaces;
    uint8_t config_value;
    uint8_t config_string;
    uint8_t attributes;
    uint8_t max_power;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct usb_interface_descriptor
{
    uint8_t length;
    uint8_t type;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t num_endpoints;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t interface_string;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct usb_endpoint_descriptor
{
    uint8_t length;
    uint8_t type;
    uint8_t endpoint_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct usb_hid_descriptor
{
    uint8_t length;
    uint8_t type;
    uint16_t hid_version;
    uint8_t country_code;
    uint8_t num_descriptors;
    uint8_t report_type;
    uint16_t report_length;
} __attribute__((packed)) usb_hid_descriptor_t;

#endif
