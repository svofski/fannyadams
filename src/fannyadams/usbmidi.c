#include <stdio.h>
#include <libopencm3/usb/usbd.h>
#include "usbcmp_descriptors.h"
#include "usbmidi.h"

/* Buffer to be used for control requests. */
uint8_t usbd_control_buffer[128];

/* SysEx identity message, preformatted with correct USB framing information */
const uint8_t sysex_identity[] = {
    0x04,	/* USB Framing (3 byte SysEx) */
    0xf0,	/* SysEx start */
    0x7e,	/* non-realtime */
    0x00,	/* Channel 0 */
    0x04,	/* USB Framing (3 byte SysEx) */
    0x7d,	/* Educational/prototype manufacturer ID */
    0x66,	/* Family code (byte 1) */
    0x66,	/* Family code (byte 2) */
    0x04,	/* USB Framing (3 byte SysEx) */
    0x51,	/* Model number (byte 1) */
    0x19,	/* Model number (byte 2) */
    0x00,	/* Version number (byte 1) */
    0x04,	/* USB Framing (3 byte SysEx) */
    0x00,	/* Version number (byte 2) */
    0x01,	/* Version number (byte 3) */
    0x00,	/* Version number (byte 4) */
    0x05,	/* USB Framing (1 byte SysEx) */
    0xf7,	/* SysEx end */
    0x00,	/* Padding */
    0x00,	/* Padding */
};

static void usbmidi_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    (void)ep;

    char buf[64];
    int len = usbd_ep_read_packet(usbd_dev, 0x01, buf, 64);

    /* This implementation treats any message from the host as a SysEx
     * identity request. This works well enough providing the host
     * packs the identify request in a single 8 byte USB message.
     */
    if (len) {
        while (usbd_ep_write_packet(usbd_dev, 0x81, sysex_identity,
                    sizeof(sysex_identity)) == 0);
    }
}

void midi_set_config(usbd_device * usbd_dev, uint16_t value)
{
    (void)value;

    usbd_ep_setup(usbd_dev, MIDI_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, 
            usbmidi_data_rx_cb);
    usbd_ep_setup(usbd_dev, MIDI_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, NULL);
}
