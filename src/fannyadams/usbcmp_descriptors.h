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
#else
    #define IFCDC                           IFSTART
#endif


#define AUDIO_CONTROL_IFACE                 ((IFCDC) + 1)
#define AUDIO_SINK_IFACE                    ((IFCDC) + 2)
#define AUDIO_SOURCE_IFACE                  ((IFCDC) + 3)

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
#define IFMIDI                              ((IFCDC) + 4)
#else
#define IFMIDI                              ((IFCDC) + 3)
#endif

#define MIDI_CONTROL_IFACE                  ((IFMIDI) + 0)
#define MIDI_STREAMING_IFACE                ((IFMIDI) + 1)

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

// An attempt at sane endpoint definition, just listing them out 
#define CDC_BULK_OUT_EP 0x01
#define CDC_BULK_IN_EP  0x82
#define CDC_COMM_EP     0x83
#if defined(FEEDBACK_EXPLICIT)
#define AUDIO_SINK_EP   0x02
#define AUDIO_SYNCH_EP  0x84
#define MIDI_OUT_EP     0x03
#define MIDI_IN_EP      0x85
#else
#error You need to define the endpoints
#endif

extern uint8_t altsetting_sink;

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
extern uint8_t altsetting_source;
#endif

extern const char* usb_strings[];
extern const struct usb_device_descriptor usbcmp_device_descr;
extern const struct usb_config_descriptor usbcmp_device_config;

uint32_t usbcmp_nstrings(void);
