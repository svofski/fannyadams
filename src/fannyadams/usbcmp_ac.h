#pragma once

#include <inttypes.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/audio.h>

typedef struct _AudioParams {
    int Mute;
    int Volume;
} AudioParams_T;

int ac_control_request(usbd_device *usbd_dev,
    struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
    void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req));
