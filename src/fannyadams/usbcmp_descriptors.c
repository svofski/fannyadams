#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/audio.h>

#include "usbcmp_descriptors.h"

enum USB_STRID {
    STRID_VENDOR = 1,
    STRID_PRODUCT,
    STRID_VERSION,
    STRID_INPUT_TERMINAL,
    STRID_OUTPUT_TERMINAL,
};

const char* usb_strings[] = {
    "(Generic USB Audio)",
    "USB Audio Device",
    "0.1",
    "Input Terminal",
    "Output Terminal",
};

const struct usb_device_descriptor usbcmp_device_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,          // Device defined at Interface level
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,
    .idProduct = 0x1234,        // 0x5740 = serial adapter
                                // Windows is boneheaded and disregards everything
                                // when it sees a known VID/PID combo. So to avoid
                                // this device being recognized as a simple serial
                                // port, a nonexistend PID is used.
    .bcdDevice = 0x0200,
    .iManufacturer = STRID_VENDOR,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_VERSION,
    .bNumConfigurations = 1,
};

#ifdef WITH_CDCACM
/*
 * This notification endpoint isn't implemented. According to CDC spec it's
 * optional, but its absence causes a NULL pointer dereference in the
 * Linux cdc_acm driver.
 */
static const struct usb_endpoint_descriptor comm_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_COMM_EP,
    .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize = 16,
    .bInterval = 255,
} };

static const struct usb_endpoint_descriptor data_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_BULK_OUT_EP,
    .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 64,
    .bInterval = 1,
}, {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_BULK_IN_EP,
    .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 64,
    .bInterval = 1,
} };

static const struct {
    struct usb_cdc_header_descriptor header;
    struct usb_cdc_call_management_descriptor call_mgmt;
    struct usb_cdc_acm_descriptor acm;
    struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
    .header = {
        .bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
        .bDescriptorType = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_HEADER,
        .bcdCDC = 0x0110,
    },
    .call_mgmt = {
        .bFunctionLength =
            sizeof(struct usb_cdc_call_management_descriptor),
        .bDescriptorType = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
        .bmCapabilities = 0,
        .bDataInterface = 1,
    },
    .acm = {
        .bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
        .bDescriptorType = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_ACM,
        .bmCapabilities = 0,
    },
    .cdc_union = {
        .bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
        .bDescriptorType = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_UNION,
        .bControlInterface = 0,
        .bSubordinateInterface0 = 1,
     }
};

static const struct usb_interface_descriptor comm_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = CDCACM_COMM_INTERFACE,
    .bAlternateSetting = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_CDC,
    .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
    .bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
    .iInterface = 0,

    .endpoint = comm_endp,

    .extra = &cdcacm_functional_descriptors,
    .extralen = sizeof(cdcacm_functional_descriptors)
} };

static const struct usb_interface_descriptor data_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = CDCACM_DATA_INTERFACE,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_DATA,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface = 0,

    .endpoint = data_endp,
} };
#endif // WITH_CDCACM

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
#define IN_COLLECTION 2 // 1 stream sink + 1 stream source
#else
#define IN_COLLECTION 1 // 1 stream sink
#endif

// Class-specific AC Interface Descriptor
static const struct {
    struct usb_audio_header_descriptor_head header;
    struct usb_audio_header_descriptor_body header_body[IN_COLLECTION]; 
    struct usb_audio_input_terminal_descriptor input_terminal;
    
    struct usb_audio_feature_unit_descriptor_head feature_unit_head;
    uint8_t feature_unit_body[2]; // master channel + channel
    struct usb_audio_feature_unit_descriptor_tail feature_unit_tail;

    struct usb_audio_output_terminal_descriptor output_terminal;

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
    // microphone or dummy feedback source
    struct usb_audio_input_terminal_descriptor source_input_terminal;
    struct usb_audio_feature_unit_descriptor_head source_feature_head;
    uint8_t source_feature_body[2];     
    struct usb_audio_feature_unit_descriptor_tail source_feature_tail;
    struct usb_audio_output_terminal_descriptor source_output_terminal;
#endif

} __attribute__((packed)) audio_control_functional_descriptors = {
    .header = {
        .bLength = sizeof(audio_control_functional_descriptors.header)  // header + iface nr size
            + sizeof(audio_control_functional_descriptors.header_body),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_HEADER,
        .bcdADC = 0x0100,
        .wTotalLength = sizeof(audio_control_functional_descriptors),   //  size w/term descriptors
        .binCollection = IN_COLLECTION,
    },
    .header_body = {
        {.baInterfaceNr = AUDIO_SINK_IFACE},    // sink streaming interface (speaker)
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
        {.baInterfaceNr = AUDIO_SOURCE_IFACE},  // source streaming interface (microphone)
#endif
    },

    // Speaker: IT:Streaming -> Feature -> OT: Speaker
    .input_terminal = {
        .bLength = USB_AUDIO_INPUT_TERMINAL_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
        .bTerminalID = AUDIO_TERMINAL_INPUT,
        .wTerminalType = USB_IO_TERMINAL_TYPE_STREAMING,
        .bAssocTerminal = 0,
        .cluster_descriptor = {
            .bNrChannels = 2,
            .wChannelConfig = USB_AUDIO_CHAN_LEFTFRONT | USB_AUDIO_CHAN_RIGHTFRONT, 
            .iChannelNames = 0,
        },
        .iTerminal = STRID_INPUT_TERMINAL,
    },

    .feature_unit_head = {
        .bLength = USB_AUDIO_FEATURE_UNIT_DESCRIPTOR_SIZE_BASE + 
            sizeof(audio_control_functional_descriptors.feature_unit_body),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_FEATURE_UNIT,
        .bUnitID = AUDIO_VOLUME_CONTROL_ID,
        .bSourceID = AUDIO_TERMINAL_INPUT,
        .bControlSize = 1,
    },
    .feature_unit_body = {
        USB_AUDIO_CONTROL_MUTE | USB_AUDIO_CONTROL_VOLUME, // master channel
        USB_AUDIO_CONTROL_MUTE | USB_AUDIO_CONTROL_VOLUME, // master channel
    },
    .feature_unit_tail = {
        .iFeature = 0,
    },

    // Output terminal is physically a speaker, its source is AUDIO_VOLUME_CONTROL_ID
    .output_terminal = {
        .bLength = USB_AUDIO_OUTPUT_TERMINAL_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_OUTPUT_TERMINAL,
        .bTerminalID = AUDIO_TERMINAL_OUTPUT,
        .wTerminalType = USB_AUDIO_OUTPUT_TERMINAL_TYPE_SPEAKER,
        .bAssocTerminal = 0,
        .bSourceId = AUDIO_VOLUME_CONTROL_ID, 
        .iTerminal = STRID_OUTPUT_TERMINAL,
    },

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
    // Microphone: IT:Microphone -> Feature -> OT:USB stream
    .source_input_terminal = {
        .bLength = USB_AUDIO_INPUT_TERMINAL_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
        .bTerminalID = AUDIO_SOURCE_TERMINAL_INPUT,
        .wTerminalType = USB_AUDIO_INPUT_TERMINAL_TYPE_MICROPHONE,
        .bAssocTerminal = 0,
        .cluster_descriptor = {
            .bNrChannels = SOURCE_CHANNELS,
            .wChannelConfig = SOURCE_CHANNEL_MAPPING,
            .iChannelNames = 0,
        },
        .iTerminal = 0,
    },
    .source_feature_head = {
        .bLength = USB_AUDIO_FEATURE_UNIT_DESCRIPTOR_SIZE_BASE + 
            sizeof(audio_control_functional_descriptors.feature_unit_body),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_FEATURE_UNIT,
        .bUnitID = AUDIO_SOURCE_VOLUME_CONTROL,
        .bSourceID = AUDIO_SOURCE_TERMINAL_INPUT,
        .bControlSize = 1,
    },
    .source_feature_body = {
        USB_AUDIO_CONTROL_MUTE | USB_AUDIO_CONTROL_VOLUME, // master channel
        USB_AUDIO_CONTROL_MUTE | USB_AUDIO_CONTROL_VOLUME, // master channel
    },
    .source_feature_tail = {
        .iFeature = 0,
    },

    .source_output_terminal = {
        .bLength = USB_AUDIO_OUTPUT_TERMINAL_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_OUTPUT_TERMINAL,
        .bTerminalID = AUDIO_SOURCE_TERMINAL_OUTPUT,
        .wTerminalType = USB_IO_TERMINAL_TYPE_STREAMING,
        .bAssocTerminal = 0,
        .bSourceId = AUDIO_SOURCE_VOLUME_CONTROL, 
        .iTerminal = 0,
    },
#endif // WITH_MICROPHONE
};

// standard interface descriptor
static const struct usb_interface_descriptor audio_control_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = AUDIO_CONTROL_IFACE,
    .bAlternateSetting = 0,
    .bNumEndpoints = 0,
    .bInterfaceClass = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_AUDIO_SUBCLASS_CONTROL,
    .bInterfaceProtocol = 0,
    .iInterface = 0,

    .extra = &audio_control_functional_descriptors,
    .extralen = sizeof(audio_control_functional_descriptors)
} };


static const struct {
    struct usb_audio_streaming_interface_descriptor speaker;// Iface 1
    struct usb_audio_type_i_iii_format_descriptor format;   // Format PCM
} __attribute__((packed)) audio_streaming_sink_functional_descriptors = {
    .speaker = {
        .bLength = USB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_STREAMING_DT_GENERAL,
        .bTerminalLink = AUDIO_TERMINAL_INPUT,
        .bDelay = 1,
        .wFormatTag = USB_AUDIO_FORMAT_PCM,
    },
    .format = {
        .bLength = USB_AUDIO_TYPE_I_III_FORMAT_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_STREAMING_DT_FORMAT_TYPE,
        .bFormatType = USB_AUDIO_FORMAT_TYPE_I,
        .bNrChannels = 2, 
        .bSubframeSize = 2, 
        .bBitResolution = 16,
        .bSamFreqType = USB_AUDIO_SAMPLING_FREQ_FIXED,
        .tSampFreq = USB_AUDIO_SAMPFREQ(48000),
    }
};

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
static const struct {
    struct usb_audio_streaming_interface_descriptor microphone;
    struct usb_audio_type_i_iii_format_descriptor format;
} __attribute__((packed)) audio_streaming_source_functional_descriptors = {
    .microphone = {
        .bLength = USB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_STREAMING_DT_GENERAL,
        .bTerminalLink = AUDIO_SOURCE_TERMINAL_OUTPUT,
        .bDelay = 1,
        .wFormatTag = USB_AUDIO_FORMAT_PCM,
    },
    .format = {
        .bLength = USB_AUDIO_TYPE_I_III_FORMAT_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_STREAMING_DT_FORMAT_TYPE,
        .bFormatType = USB_AUDIO_FORMAT_TYPE_I,
        .bNrChannels = 1,
        .bSubframeSize = 2,
        .bBitResolution = 16,
        .bSamFreqType = USB_AUDIO_SAMPLING_FREQ_FIXED,
        .tSampFreq = USB_AUDIO_SAMPFREQ(48000),
    }
};
#endif

static const struct usb_audio_streaming_endpoint_descriptor as_sink_endp = {
    .bLength = USB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR_SIZE,
    .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
    .bDescriptorSubtype = USB_AUDIO_EP_GENERAL,
    .bmAttributes = 0,
    .bLockDelayUnits = 0,//1, // milliseconds
    .wLockDelay = 0, //4,     // 4ms
};

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
static const struct usb_audio_streaming_endpoint_descriptor as_source_endp = {
    .bLength = USB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR_SIZE,
    .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
    .bDescriptorSubtype = USB_AUDIO_EP_GENERAL,
    .bmAttributes = 0,
    .bLockDelayUnits = 0,
    .wLockDelay = 0,
};
#endif

static const struct usb_endpoint_descriptor audio_sink_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = AUDIO_SINK_EP,
#if defined(FEEDBACK_EXPLICIT) || defined(FEEDBACK_IMPLICIT)
    .bmAttributes = USB_ENDPOINT_ATTR_ISO_ASYNC,
#else
    .bmAttributes = USB_ENDPOINT_ATTR_ISO_SYNC,
#endif  
    .wMaxPacketSize = AUDIO_SINK_PACKET_SIZE,
    .bInterval = 1,
    .bRefresh = 0,
#if defined(FEEDBACK_EXPLICIT)
    .bSynchAddress = AUDIO_SYNCH_EP,
#else
    .bSynchAddress = 0,
#endif
    .extra = &as_sink_endp,
    .extralen = sizeof(as_sink_endp)
    },
#if defined(FEEDBACK_EXPLICIT)
    {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = AUDIO_SYNCH_EP,
    .bmAttributes = USB_ENDPOINT_ATTR_ISOCHRONOUS,
    .wMaxPacketSize = 3,
    .bInterval = 1,
    .bRefresh = 4, // Explicit feedback value update rate.  
                   // Specified in powers of 2: from 1 = 2ms to 9 = 512ms
                   // Windows seems to hate values less than 4.
    .bSynchAddress = 0,
    }
#endif
};

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
static const struct usb_endpoint_descriptor audio_source_endp[] = {{
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = AUDIO_SOURCE_EP,
#if defined(WITH_MICROPHONE)
    // Microphone audio source
    .bmAttributes = USB_ENDPOINT_ATTR_ISO_SYNC,
#else 
    // Implicit feedback endpoint
    .bmAttributes = USB_ENDPOINT_ATTR_ISO_ASYNC | USB_ENDPOINT_USAGE_IMPLICIT_FEEDBACK,
#endif
    .wMaxPacketSize = AUDIO_SOURCE_PACKET_SIZE,
    .bInterval = 1,
    .bRefresh = 0,
    .bSynchAddress = 0,

    .extra = &as_source_endp,
    .extralen = sizeof(as_source_endp)
}};
#endif

// Audio sink: USB Speaker
static const struct usb_interface_descriptor audio_streaming_sink_iface[] = {
    {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = AUDIO_SINK_IFACE,
        .bAlternateSetting = 0,
        .bNumEndpoints = 0,
        .bInterfaceClass = USB_CLASS_AUDIO,
        .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    {
        .bLength = USB_AUDIO_INTERFACE_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = AUDIO_SINK_IFACE,
        .bAlternateSetting = 1,
        .bNumEndpoints = sizeof(audio_sink_endp)/(sizeof(audio_sink_endp[0])),
        .bInterfaceClass = USB_CLASS_AUDIO,
        .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = audio_sink_endp,

        .extra = &audio_streaming_sink_functional_descriptors,
        .extralen = sizeof(audio_streaming_sink_functional_descriptors)
    }   
};

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
// Audio source: USB Microphone/Line In
static const struct usb_interface_descriptor audio_streaming_source_iface[] = {
    {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = AUDIO_SOURCE_IFACE,
        .bAlternateSetting = 0,
        .bNumEndpoints = 0,
        .bInterfaceClass = USB_CLASS_AUDIO,
        .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    {
        .bLength = USB_AUDIO_INTERFACE_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = AUDIO_SOURCE_IFACE,
        .bAlternateSetting = 1,
        .bNumEndpoints = 1,
        .bInterfaceClass = USB_CLASS_AUDIO,
        .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = audio_source_endp,

        .extra = &audio_streaming_source_functional_descriptors,
        .extralen = sizeof(audio_streaming_source_functional_descriptors)
    }   
};
#endif

uint8_t altsetting_sink = 0;

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
uint8_t altsetting_source = 0;
#endif

static const struct usb_interface ifaces[] = {
#ifdef WITH_CDCACM
    {
    .num_altsetting = 1,
    .altsetting = comm_iface,
    },
    {
    .num_altsetting = 1,
    .altsetting = data_iface,
    },
#endif
    {
    .num_altsetting = 1,
    .altsetting = audio_control_iface,
    },
    {
    .num_altsetting = 2,
    .cur_altsetting = &altsetting_sink,
    .altsetting = audio_streaming_sink_iface,
    },
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
    {
    .num_altsetting = 2,
    .cur_altsetting = &altsetting_source,
    .altsetting = audio_streaming_source_iface,
    },
#endif
    };

const struct usb_config_descriptor usbcmp_device_config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0,          // Length of the total configuration block, including this descriptor, in bytes.
    .bNumInterfaces = sizeof(ifaces)/sizeof(ifaces[0]), 
    .bConfigurationValue = 1,   
    .iConfiguration = 0,
    .bmAttributes = USB_CONFIG_ATTR_DEFAULT,
    .bMaxPower = 0x32,

    .interface = ifaces,
};

uint32_t usbcmp_nstrings(void) {
    return sizeof(usb_strings)/sizeof(usb_strings[0]);
}