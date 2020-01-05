#pragma once

#define WITH_CDCACM
//#define WITH_MICROPHONE
#define FEEDBACK_EXPLICIT
//#define FEEDBACK_IMPLICIT

#define XSTR(x) STR(x)
#define STR(x) #x

#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#define VAR_NAME_VALUE(var) #var "="  VALUE(var)

#define SOURCE_CHANNELS                     1
#if SOURCE_CHANNELS == 1
  #define SOURCE_CHANNEL_MAPPING (USB_AUDIO_CHAN_MONO)
  #define SOURCE_SAMPLE_SIZE                2
#else
  #define SOURCE_CHANNEL_MAPPING (USB_AUDIO_CHAN_LEFTFRONT | USB_AUDIO_CHAN_RIGHTFRONT)
#define SOURCE_SAMPLE_SIZE                  4
#endif
#define SINK_SAMPLE_SIZE                    4       // Stereo: 16 bits * 2

#define IFSTART                             (-1)
#define EPSTART                             0

#ifdef WITH_CDCACM
    #define CDCACM_COMM_INTERFACE           ((IFSTART) + 1)
    #define CDCACM_DATA_INTERFACE           ((IFSTART) + 2)
    #define IFCDC                           (CDCACM_DATA_INTERFACE)

    #define CDC_BULK_IN_EP                  (0200 | ((EPSTART) + 2))    // 0x82
    #define CDC_BULK_OUT_EP                 ((EPSTART) + 1)             // 0x01
    #define CDC_COMM_EP                     (0200 | ((EPSTART) + 3))    // 0x83
    #define EPCDC                           (0177 & CDC_BULK_OUT_EP)
    #define IN_AEP                          (CDC_COMM_EP+1)
    #define OUT_AEP                         ((CDC_BULK_OUT_EP) + 1)
#else
    #define IFCDC                           IFSTART
    #define IN_AEP                          (0200 | (EPSTART + 1))
    #define OUT_AEP                         (EPSTART + 1)
#endif

#define AUDIO_CONTROL_IFACE                 ((IFCDC) + 1)
#define AUDIO_SINK_IFACE                    ((IFCDC) + 2)
#define AUDIO_SOURCE_IFACE                  ((IFCDC) + 3)
#define IFAUDIO                             AUDIO_SOURCE_IFACE

#define AUDIO_SINK_EP                       (OUT_AEP)                   // 0x01

#if defined(FEEDBACK_EXPLICIT)
    #define AUDIO_SYNCH_EP                  (IN_AEP)                    // 0x81
    #define IN_SRC                          (AUDIO_SYNCH_EP + 1)
#else
    #define IN_SRC                          IN_AEP
#endif

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
    #define AUDIO_SOURCE_EP                 (IN_SRC)                    // 0x82
#endif

#pragma message(VAR_NAME_VALUE(CDCACM_COMM_INTERFACE))
#pragma message(VAR_NAME_VALUE(CDCACM_DATA_INTERFACE))
#pragma message(VAR_NAME_VALUE(AUDIO_CONTROL_IFACE))
#pragma message(VAR_NAME_VALUE(AUDIO_SINK_IFACE))
#pragma message(VAR_NAME_VALUE(AUDIO_SOURCE_IFACE))

// Next IFACE starts with IFAUDIO + 1
// Next EP starts with EPSOURCE + 1

// Packet sizes must reserve space for one extra sample for rate adjustments
#define AUDIO_SINK_PACKET_SIZE              (USB_AUDIO_PACKET_SIZE(48000,2,16) + SINK_SAMPLE_SIZE)
#define AUDIO_SOURCE_PACKET_SIZE            (USB_AUDIO_PACKET_SIZE(48000,SOURCE_CHANNELS,16) + SOURCE_SAMPLE_SIZE)

// Entity IDs for audio sink terminals: input, feature control, output
#define AUDIO_TERMINAL_INPUT                1   // USB Stream
#define AUDIO_VOLUME_CONTROL_ID             2   // Mute/Volume Control
#define AUDIO_TERMINAL_OUTPUT               3   // Speaker

// Entity IDs for audio source terminals: input, output
#define AUDIO_SOURCE_TERMINAL_INPUT         4   // Line In
#define AUDIO_SOURCE_VOLUME_CONTROL         5
#define AUDIO_SOURCE_TERMINAL_OUTPUT        6   // USB Stream

extern uint8_t altsetting_sink;

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
extern uint8_t altsetting_source;
#endif

extern const char* usb_strings[];
extern const struct usb_device_descriptor usbcmp_device_descr;
extern const struct usb_config_descriptor usbcmp_device_config;

uint32_t usbcmp_nstrings(void);
