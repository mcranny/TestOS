#ifndef TESTOS_UEFI_USB_HID_H
#define TESTOS_UEFI_USB_HID_H

#include "types.h"
#include "usb/usb.h"

void hid_register_device(uint32_t slot, const usb_device_descriptor_t *desc);
void hid_initialize(void);
void hid_poll(void);

#endif
