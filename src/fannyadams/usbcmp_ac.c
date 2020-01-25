#include <inttypes.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/audio.h>

#include "usbcmp_descriptors.h"
#include "usbcmp_ac.h"
#include "xprintf.h"

AudioParams_T AudioParams = {
    .Mute = 0,
    .Volume = 100,
};

int ac_control_request(usbd_device *usbd_dev,
    struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
    void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
    (void)complete;
    (void)buf;
    (void)usbd_dev;

    int cs = req->wValue >> 8;
    int cn = req->wValue & 0377;
    int terminal = req->wIndex >> 8;
    int iface = req->wIndex & 0377;

    xprintf("ac_control_request: cs=%x chan=%x entity=%x iface=%x len=%x: ", 
        cs, cn, terminal, iface, req->wLength);

    switch (req->bRequest) {
    case USB_AUDIO_REQ_GET_CUR:
        // Get current terminal setting: 
        // Value = 0x200 = Control Selector; Index = 0x201 Terminal ID and Interface
        {
            xprintf("USB_AUDIO_REQ_GET_CUR\r\n");
            if (iface == AUDIO_CONTROL_IFACE && terminal == AUDIO_VOLUME_CONTROL_ID) {
                switch (cs) {
                    case 1:
                        // CS 1 = MUTE
                        (*buf)[0] = (uint8_t) AudioParams.Mute;
                        *len = 1;
                        break;
                    case 2:
                        // CS 2 = VOL?
                        (*((uint16_t**)buf))[0] = AudioParams.Volume;
                        xprintf("GET VOLUME: %x\n", AudioParams.Volume);
                        break;
                    default:
                        xprintf("UNKNOWN CS=%d\r\n", cs);
                        *len = 0;
                }
            }
        }
        return USBD_REQ_HANDLED;
    case USB_AUDIO_REQ_SET_CUR:
        {
            // proper handling would receive data from EP0
            xprintf("USB_AUDIO_REQ_SET_CUR\r\n");
            xprintf(" data=%x %x\r\n", (*buf)[0], (*buf)[1]);
            if (iface == AUDIO_CONTROL_IFACE && terminal == AUDIO_VOLUME_CONTROL_ID) {
                switch (cs) {
                    case 1:
                        // CS 1 = MUTE
                        AudioParams.Mute = (*buf)[0];
                        xprintf("MUTE=%d\n", AudioParams.Mute);
                        break;
                    case 2:
                        // CS 2 = VOLUME
                        AudioParams.Volume = (*((uint16_t**)buf))[0];
                        xprintf("SET VOLUME: %x\n", AudioParams.Volume);
                        break;
                    default:
                        xprintf("UNKNOWN CS=%d\r\n", cs);
                }
            }

            return USBD_REQ_HANDLED; 
        }
    case USB_AUDIO_REQ_GET_MIN:
        {
            xprintf("USB_AUDIO_REQ_GET_MIN\r\n");
            (*buf)[0] = 0;
            (*buf)[1] = 0;
            *len = 2;
        }
        return USBD_REQ_HANDLED; 
    case USB_AUDIO_REQ_GET_MAX:
        {
            xprintf("USB_AUDIO_REQ_GET_MAX\r\n");
            (*buf)[0] = 0x00;
            (*buf)[1] = 0x01;
            *len = 2;
        }
        return USBD_REQ_HANDLED; 
    case USB_AUDIO_REQ_GET_RES:
        {
            xprintf("USB_AUDIO_REQ_GET_RES\r\n");
            (*buf)[0] = 0x10;
            (*buf)[1] = 0x00;
            *len = 2;
        }
        return USBD_REQ_HANDLED;
    case USB_AUDIO_REQ_SET_RES:
        {
            xprintf("USB_AUDIO_REQ_SET_RES\r\n");
            xprintf(" data=%x %x\r\n", (*buf)[0], (*buf)[1]);
            return USBD_REQ_HANDLED;
        }
    }
    return USBD_REQ_NOTSUPP;
}
