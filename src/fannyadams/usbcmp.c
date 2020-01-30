#include <stdlib.h>
#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/audio.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/otg_common.h>
#include <libopencm3/stm32/otg_fs.h>
#include <libopencm3/stm32/otg_hs.h>

#include "usbcmp.h"
#include "systick.h"
#include "xprintf.h"
#include "usrat.h"
#include "i2s.h"
#include "audiobuf.h"

#include "util.h"

#include "usbcmp_descriptors.h"
#include "usbcmp_cdc.h"
#include "usbcmp_ac.h"
#include "usbmidi.h"

usbd_device *device = NULL;


static volatile uint32_t npackets_fb;
static volatile uint32_t flag;
static volatile uint32_t pid;
static volatile uint32_t flush_count;

static volatile uint32_t npackets_rx;
static volatile uint16_t fb_timer_last;

static volatile uint32_t feedback_value = 48 << 14;
static volatile uint32_t sink_buffer_fullness;

// -- descriptors here

/* Buffer to be used for control requests. Needs to be large to fit all those descriptors. */
static uint8_t usbd_control_buffer[512]; // vcp + audio (no mic) + midi = 362

static int common_control_request(usbd_device *usbd_dev,
    struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
    void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
    (void)complete;
    (void)buf;
    (void)usbd_dev;

    //xprintf("control_request: %x Value=%x Index=%x \n", req->bRequest, req->wValue, req->wIndex);

    switch (req->bRequest) {
        case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
        case USB_CDC_REQ_SET_LINE_CODING:
            return cdc_control_request(usbd_dev, req, buf, len, complete);

        case USB_AUDIO_REQ_GET_CUR:
        case USB_AUDIO_REQ_SET_CUR:
        case USB_AUDIO_REQ_GET_MIN: // 0x82
        case USB_AUDIO_REQ_GET_MAX: // 0x83
        case USB_AUDIO_REQ_GET_RES: // 0x84 - resolution of volume control
        case USB_AUDIO_REQ_SET_RES: // 0x04
            return ac_control_request(usbd_dev, req, buf, len, complete);
    }
    return 0;
}

// "The SOF pulse signal is also internally connected to the TIM2 input trigger, 
// so that the input capture feature, the output compare feature and the timer
// can be triggered by the SOF pulse. The TIM2 connection is enabled
// through the ITR1_RMP bits of the TIM2 option register (TIM2_OR)."

// Connect SOF to TIM2 trigger. Capture timer value on SOF and reset/restart
// the timer. This happens without generating interrupts. The captured value
// is polled in send_explicit_fb().

static void feedback_timer_stop(void) {
    timer_disable_counter(TIM2);
}

static void feedback_timer_start(void) {
    rcc_periph_clock_enable(RCC_TIM2);
    feedback_timer_stop();
    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    timer_set_prescaler(TIM2, 0);

    timer_ic_disable(TIM2, TIM_IC1);                        // channel must be disabled first
    timer_disable_oc_output(TIM2, TIM_OC1);                 // disable output
    timer_ic_set_input(TIM2, TIM_IC1, TIM_IC_IN_TRC);       // input is TRC
    timer_ic_set_prescaler(TIM2, TIM_IC1, TIM_IC_PSC_OFF);  // prescaler off
    timer_ic_set_polarity(TIM2, TIM_IC1, TIM_IC_RISING);    // capture rising edge
    timer_ic_enable(TIM2, TIM_IC1);                         // enable input capture channel 1


    timer_set_option(TIM2, TIM2_OR_ITR1_RMP_OTG_FS_SOF);    // SOF is trigger 1
    timer_slave_set_mode(TIM2, TIM_SMCR_SMS_RM);            // reset counter on trigger (SOF)
    timer_slave_set_trigger(TIM2, TIM_SMCR_TS_ITR1);        // use trigger 1 for slaving

    timer_enable_counter(TIM2);
}

static void send_explicit_fb(usbd_device *usbd_dev, size_t fullness) 
{
    // I'm not sure why, but the measured time between SOF events seems to be
    // slightly larger than the period at which DMA consumes samples.
    // Here's a bit of correction based on buffer load.
    uint32_t tim2 = TIM_CCR1(TIM2);
    if (fullness < AUDIO_SINK_PACKET_SIZE * 2) {
        tim2 += -8;
    } else if (fullness < AUDIO_SINK_PACKET_SIZE * 3) {
        tim2 += -16;
    } else {
        tim2 += -32;
    }
    feedback_value = (tim2 << 14) / 1750;
#if defined(FEEDBACK_EXPLICIT)
    usbd_ep_write_packet(usbd_dev, AUDIO_SYNCH_EP, (uint32_t*)&feedback_value, 3);
#else
    STFU(usbd_dev);
#endif
}

static void flush_synch_ep(void) {
    flush_count++;
#if defined(FEEDBACK_EXPLICIT)
    OTG_FS_GRSTCTL |= OTG_GRSTCTL_TXFFLSH | ((AUDIO_SYNCH_EP & 0177) << 6);

    while((OTG_FS_GRSTCTL & OTG_GRSTCTL_TXFFLSH) == OTG_GRSTCTL_TXFFLSH);
#endif
}

#define DSTS_FNSOF_ODD_MASK (1 << 8)

static void incomplete(void) {
    pid = OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK;
    if (flag && --flag == 0) {
        flush_synch_ep();
    }
}

#if defined(FEEDBACK_EXPLICIT)
static void audio_synch_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    STFU(ep);
    // TODO: when the output is restarted, a callback may arrive
    // before the feedback loop is initiated from audio_data_rx_cb()
    if (npackets_rx < 64) {
        xputchar('~');
        return;
    }
    if ((OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK) == pid) {
        send_explicit_fb(usbd_dev, sink_buffer_fullness);
        npackets_fb++;
        flag = 1;
    }
}
#endif

static void audio_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    (void)ep;

    if (altsetting_sink) {
        npackets_rx++;

        int read = 0;
        for(;;) {
            int vacant = asink_vacant();
            int len = usbd_ep_read_packet(usbd_dev, AUDIO_SINK_EP, 
                    asink_head(), vacant);
            if (len == 0) {
                break;
            }
            read += len;
            asink_advance_head(len);
        }

        sink_buffer_fullness = asink_fullness();
        
        // Initiate feedback
        if (((npackets_rx == 64) && (OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK) == pid) ||
            ((npackets_rx == 65) && (OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK) == pid)) {
            //xputchar('*');
            send_explicit_fb(usbd_dev, sink_buffer_fullness);
            flag = 1;
        }

        //xputchar((read == 196) ? '+' : (read == 188 ? '-' : '.'));
        if (npackets_rx % 20 == 0) {
            xprintf("fb=%d.%03d |%d| ->%d f%d read=%d\n", 
                    feedback_value >> 14, ((feedback_value>>4) & 0x3ff)*1000/1024, 
                    sink_buffer_fullness, npackets_fb, flush_count, read);
        }
    }
}


#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
static void audio_data_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    (void)ep;
    static size_t tosend = AUDIO_SOURCE_PACKET_SIZE - SOURCE_SAMPLE_SIZE;

    if (altsetting_source) {
        // shift out current packet: this copies audio_source_buffer into EP fifo
        uint16_t len = usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, audio_source_buffer, tosend);
        (void)len; // stfu

        // prepare the new one
        tosend = 48 * SOURCE_SAMPLE_SIZE;

        if (sink_buffer_fullness > AUDIO_SINK_PACKET_SIZE*2) {
            if (npackets_rx % 16 == 0) {
                // too much, one sample less perhaps
                tosend -= SOURCE_SAMPLE_SIZE;
            }
        }
#if defined(WITH_MICROPHONE)
        size_t tocopy = tosend;
        size_t tail = 0;
        for(;tocopy > 0;) {
            if (mic_sample_ofs + tocopy < sizeof(mic_sample)) {
                memcpy(audio_source_buffer + tail, mic_sample + mic_sample_ofs, tocopy);
                mic_sample_ofs += tocopy;
                tocopy = 0;
            } else {
                tail = sizeof(mic_sample) - mic_sample_ofs;
                memcpy(audio_source_buffer, mic_sample + mic_sample_ofs, tail);
                mic_sample_ofs = 0;
                tocopy -= tail;
            }
        }
#endif
        xprintf(">%d>", tosend);
    }
}
#endif

static void set_altsetting_cb(usbd_device *usbd_dev, uint16_t index, uint16_t value) {
    STFU(usbd_dev);
    xprintf("set_altsetting_cb: iface=%d value=%d\n", index, value);
    if (index == AUDIO_SOURCE_IFACE) {
        if (value == 1) {
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
            xprintf("Starting to send data");
            usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, NULL, 0);
#endif
        }
    } else if (index == AUDIO_SINK_IFACE) {
        if (value == 1) {
            npackets_rx = 0;
            npackets_fb = 0;
            asink_init();
            pid = OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK;
            flush_synch_ep();
        } else {
            flush_synch_ep();
        }
    }
}

static void set_config_cb(usbd_device *usbd_dev, uint16_t wValue)
{
    (void)wValue;

    xprintf("set_config wValue=%d\n", wValue);

    cdc_set_config(usbd_dev, wValue);

    usbd_ep_setup(usbd_dev, AUDIO_SINK_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, AUDIO_SINK_PACKET_SIZE, 
        audio_data_rx_cb);
#if defined(FEEDBACK_EXPLICIT)
    usbd_ep_setup(usbd_dev, AUDIO_SYNCH_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, 3, audio_synch_tx_cb);
#endif
#if defined(FEEDBACK_IMPLICIT) || defined(WITH_MICROPHONE)
    usbd_ep_setup(usbd_dev, AUDIO_SOURCE_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, AUDIO_SOURCE_PACKET_SIZE, 
        audio_data_tx_cb);
#endif
    usbd_register_control_callback(usbd_dev,
                USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE, //type
                USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,  //mask
                common_control_request);

    midi_set_config(usbd_dev, wValue);
}

//static void fill_buffer(void) {
//    mic_sample_ofs = 0;
//    for (uint32_t i = 0; i < sizeof(mic_sample); i += 2) {
//#ifdef WITH_MICROPHONE
//        ((uint16_t*)mic_sample)[i/2] = 128*(i - sizeof(mic_sample)/2);
//#else
//        ((uint16_t*)mic_sample)[i/2] = 0;
//#endif
//    }
//    for (uint32_t i = 0; i < sizeof(audio_source_buffer); i++) {
//        audio_source_buffer[i] = 0;
//    }
//    mic_sample_ofs = 0;
//}

void USBCMP_Poll(void) {
    usbd_poll(device);
}

void USBCMP_Setup(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_OTGFS);

    feedback_timer_start();

    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
            GPIO9 | GPIO11 | GPIO12);
    gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);

    device = usbd_init(&otgfs_usb_driver, 
            &usbcmp_device_descr, &usbcmp_device_config,
            usb_strings, usbcmp_nstrings(),
            usbd_control_buffer, sizeof(usbd_control_buffer));

    usbd_register_set_altsetting_callback(device, set_altsetting_cb);
    usbd_register_set_config_callback(device, set_config_cb);

#if defined(FEEDBACK_EXPLICIT)
    usbd_register_incomplete_callback(device, incomplete);
#endif

//    fill_buffer();

    xprintf("USBCMP: Audio Control: IF %d Sink: IF %d ", 
        AUDIO_CONTROL_IFACE, AUDIO_SINK_IFACE);
#ifdef AUDIO_SOURCE_IFACE
    xprintf("Source: IF %d ", AUDIO_SOURCE_IFACE);
#endif
    xprintf("\n");
#ifdef WITH_CDCACM
    xprintf("USBCMP: CDC Comm: IF %d (EP %02x) CDC Data: IF %d (EP IN %02x OUT %02x)\n", 
        CDCACM_COMM_INTERFACE, CDC_COMM_EP,
        CDCACM_DATA_INTERFACE, CDC_BULK_IN_EP, CDC_BULK_OUT_EP);
#endif
    xprintf("USBCMP: Sink EP %02x |%d| Feedback type: ", AUDIO_SINK_EP, AUDIO_SINK_PACKET_SIZE);
#if defined(FEEDBACK_IMPLICIT)
    xprintf("IMPLICIT via Source EP %02x |%d| ", AUDIO_SOURCE_EP, AUDIO_SOURCE_PACKET_SIZE);
#else
    xprintf("EXPLICIT via Feedback EP %02x ", AUDIO_SYNCH_EP);
#endif
#if defined(WITH_MICROPHONE)
    xprintf("Microphone EP %02x |%d|", AUDIO_SOURCE_EP, AUDIO_SOURCE_PACKET_SIZE);
#else
    xprintf("No microphone");
#endif
    xprintf("\n");
    xprintf("USBCMP: buffer sizes: sink=|%d| ", asink_size());
#if defined(FEEDBACK_IMPLICIT) || defined(WITH_MICROPHONE)
    xprintf("source=|%d|", sizeof(audio_source_buffer));
#endif
    xprintf("\n");
}

