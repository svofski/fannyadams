#include <inttypes.h>
#include <stdlib.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

#include "usbcmp_descriptors.h"
#include "usbcmp_cdc.h"

#ifdef WITH_CDCACM
static char cdc_buf[64];
#endif

int cdc_control_request(usbd_device *usbd_dev,
    struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
    void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req)) 
{
    (void)usbd_dev;
    (void)buf;
    (void)complete;

    switch (req->bRequest) {
    case USB_CDC_REQ_SET_CONTROL_LINE_STATE: {
        /*
         * This Linux cdc_acm driver requires this to be implemented
         * even though it's optional in the CDC spec, and we don't
         * advertise it in the ACM functional descriptor.
         */
        return USBD_REQ_HANDLED;
        }
    case USB_CDC_REQ_SET_LINE_CODING:
        if (*len < sizeof(struct usb_cdc_line_coding)) {
            return USBD_REQ_NOTSUPP;
        }
        return USBD_REQ_HANDLED;
    }
    return USBD_REQ_NOTSUPP;
}

#ifdef WITH_CDCACM
static void cdcacm_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    (void)ep;

    size_t len = usbd_ep_read_packet(usbd_dev, CDC_BULK_OUT_EP, cdc_buf, sizeof(cdc_buf) - 1);

    if (len) {
        size_t wrlen = usbd_ep_write_packet(usbd_dev, CDC_BULK_IN_EP, cdc_buf, len);
        if (wrlen == 0) {
            // error!
        }
    }
}
#endif

void cdc_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
    (void)usbd_dev;
    (void)wValue;

#ifdef WITH_CDCACM
    usbd_ep_setup(usbd_dev, CDC_BULK_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
    usbd_ep_setup(usbd_dev, CDC_BULK_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, NULL);
    usbd_ep_setup(usbd_dev, CDC_COMM_EP, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);
#endif
}