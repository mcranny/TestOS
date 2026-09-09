#include "usb/hid.h"
#include "usb/usb.h"
#include "input/input.h"
#include "platform.h"
#include "lib/string.h"

#define HID_MAX_DEVICES 4U

typedef struct hid_device
{
    int active;
    uint32_t slot;
    uint8_t protocol;
    uint8_t endpoint;
    uint16_t max_packet;
    uint8_t last_keys[6];
} hid_device_t;

static hid_device_t hid_devices[HID_MAX_DEVICES];
static uint32_t hid_device_count;

static input_keycode_t hid_usage_to_keycode(uint8_t usage)
{
    if (usage >= 0x04U && usage <= 0x1DU) {
        return (input_keycode_t)(INPUT_KEY_A + (usage - 0x04U));
    }
    if (usage >= 0x1EU && usage <= 0x27U) {
        return (input_keycode_t)(INPUT_KEY_1 + (usage - 0x1EU));
    }
    switch (usage) {
        case 0x28U: return INPUT_KEY_ENTER;
        case 0x29U: return INPUT_KEY_ESC;
        case 0x2AU: return INPUT_KEY_BACKSPACE;
        case 0x2BU: return INPUT_KEY_TAB;
        case 0x2CU: return INPUT_KEY_SPACE;
        case 0x50U: return INPUT_KEY_LEFT;
        case 0x51U: return INPUT_KEY_RIGHT;
        case 0x52U: return INPUT_KEY_ARROW_UP;
        case 0x53U: return INPUT_KEY_ARROW_DOWN;
        default: return INPUT_KEY_NONE;
    }
}

void hid_register_device(uint32_t slot, const usb_device_descriptor_t *desc)
{
    hid_device_t *dev;

    if (desc == NULL || hid_device_count >= HID_MAX_DEVICES) {
        return;
    }

    dev = &hid_devices[hid_device_count++];
    memset(dev, 0, sizeof(*dev));
    dev->active = 1;
    dev->slot = slot;
    dev->endpoint = 0x81U;
    dev->max_packet = 8U;

    if (desc->device_class == USB_CLASS_HID ||
        desc->device_protocol == USB_PROTO_KEYBOARD ||
        desc->device_protocol == USB_PROTO_MOUSE) {
        dev->protocol = desc->device_protocol;
        klog_uint(KLOG_INFO, "HID", "Registered slot ", slot);
    }
}

static void hid_keyboard_report(hid_device_t *dev, const uint8_t *report, uint32_t length)
{
    input_event_t event;
    uint32_t index;
    uint8_t modifiers;
    int key_index;

    if (dev == NULL || report == NULL || length < 8U) {
        return;
    }

    modifiers = report[0];
    input_set_shift(((modifiers & 0x22U) != 0) ? 1 : 0);

    for (key_index = 0; key_index < 6; key_index++) {
        uint8_t usage = report[2U + (uint32_t)key_index];
        int seen = 0;
        input_keycode_t key;

        if (usage == 0U) {
            continue;
        }

        for (index = 0; index < 6U; index++) {
            if (usage == dev->last_keys[index]) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }

        key = hid_usage_to_keycode(usage);
        if (key == INPUT_KEY_NONE) {
            continue;
        }

        event.type = INPUT_KEY_DOWN;
        event.key = key;
        event.mouse_x = 0;
        event.mouse_y = 0;
        event.mouse_button = 0;
        event.mouse_pressed = 0;
        input_push_event(&event);
    }

    for (key_index = 0; key_index < 6; key_index++) {
        uint8_t old_usage = dev->last_keys[key_index];
        int still_down = 0;
        input_keycode_t key;
        uint32_t i;

        if (old_usage == 0U) {
            continue;
        }

        for (i = 2; i < 8U; i++) {
            if (report[i] == old_usage) {
                still_down = 1;
                break;
            }
        }
        if (still_down) {
            continue;
        }

        key = hid_usage_to_keycode(old_usage);
        if (key == INPUT_KEY_NONE) {
            continue;
        }

        event.type = INPUT_KEY_UP;
        event.key = key;
        event.mouse_x = 0;
        event.mouse_y = 0;
        event.mouse_button = 0;
        event.mouse_pressed = 0;
        input_push_event(&event);
    }

    for (index = 0; index < 6U; index++) {
        dev->last_keys[index] = report[2U + index];
    }
}

static void hid_mouse_report(hid_device_t *dev, const uint8_t *report, uint32_t length)
{
    input_event_t event;

    (void)dev;
    if (report == NULL || length < 3U) {
        return;
    }

    event.type = INPUT_MOUSE_MOVE;
    event.key = INPUT_KEY_NONE;
    event.mouse_x = (int32_t)(int8_t)report[1];
    event.mouse_y = (int32_t)(int8_t)report[2];
    event.mouse_button = 0;
    event.mouse_pressed = 0;
    if (event.mouse_x != 0 || event.mouse_y != 0) {
        input_push_event(&event);
    }

    if ((report[0] & 0x01U) != 0) {
        event.type = INPUT_MOUSE_BUTTON;
        event.mouse_button = 0;
        event.mouse_pressed = 1;
        input_push_event(&event);
    }
    if ((report[0] & 0x02U) != 0) {
        event.type = INPUT_MOUSE_BUTTON;
        event.mouse_button = 1;
        event.mouse_pressed = 1;
        input_push_event(&event);
    }
}

void hid_initialize(void)
{
    klog(KLOG_INFO, "HID", "Ready");
}

void hid_poll(void)
{
    uint32_t index;

    /*
     * Interrupt IN transfers are not wired through xHCI yet. Do not invent
     * zero-filled reports — that would corrupt per-device key state and spam
     * mouse events. When a transfer path exists, fetch into `report` and call
     * the parsers with the matching device.
     */
    for (index = 0; index < hid_device_count; index++) {
        hid_device_t *dev = &hid_devices[index];
        uint8_t report[8];
        uint32_t report_len = 0;

        if (!dev->active) {
            continue;
        }

        (void)dev->slot;
        memset(report, 0, sizeof(report));
        /* Placeholder: report_len stays 0 until xHCI interrupt IN is implemented. */
        if (report_len == 0) {
            continue;
        }

        if (dev->protocol == USB_PROTO_MOUSE) {
            hid_mouse_report(dev, report, report_len);
        } else {
            hid_keyboard_report(dev, report, report_len);
        }
    }
}
