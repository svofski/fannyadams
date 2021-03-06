#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/audio.h>
#include <libopencm3/usb/midi.h>

#include "usbcmp_descriptors.h"

enum USB_STRID {
    STRID_VENDOR = 1,
    STRID_PRODUCT,
    STRID_VERSION,
    STRID_INPUT_TERMINAL,
    STRID_OUTPUT_TERMINAL,
};

const char* usb_strings[] = {
    "svofski",
    "Fanny Adams",
    "0.1",
    "Input Terminal",
    "Output Terminal",
};

const struct usb_device_descriptor usbcmp_device_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0xef,          // Use interface association descriptor
    .bDeviceSubClass = 2,
    .bDeviceProtocol = 1,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x6666,         // prototype vendor id
    .idProduct = 0x1235,        // 0x5740 = serial adapter
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
    .bInterfaceNumber = CDCACM_COMM_INTERFACE, // 0
    .bAlternateSetting = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_CDC,
    .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
    .bInterfaceProtocol = USB_CDC_PROTOCOL_NONE, //USB_CDC_PROTOCOL_AT,
    .iInterface = 0,

    .endpoint = comm_endp,

    .extra = &cdcacm_functional_descriptors,
    .extralen = sizeof(cdcacm_functional_descriptors)
} };

static const struct usb_interface_descriptor data_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = CDCACM_DATA_INTERFACE, // 1
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_DATA,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface = 0,

    .endpoint = data_endp,
} };

// An interface association allows the device to group a set of interfaces to
// represent one logical device to be managed by one host driver.
static const struct usb_iface_assoc_descriptor cdc_acm_interface_association = {
    // The size of an interface association descriptor: 8
    .bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
    // A value of 11 indicates that this descriptor describes an interface
    // association.
    .bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
    // The first interface that is part of this group.
    .bFirstInterface = CDCACM_COMM_INTERFACE,
    // The number of included interfaces. This implies that the bundled
    // interfaces must be continugous.
    .bInterfaceCount = 2,
    // The class, subclass, and protocol of device represented by this
    // association. In this case a communication device.
    .bFunctionClass = USB_CLASS_CDC,
    // Using Abstract Control Model
    .bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
    // With AT protocol (or Hayes compatible).
    .bFunctionProtocol = USB_CDC_PROTOCOL_AT,
    // A string representing this interface. Zero means not provided.
    .iFunction = 0,
};

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
    .bInterfaceNumber = AUDIO_CONTROL_IFACE, // 2
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
        .bInterfaceNumber = AUDIO_SINK_IFACE, // 3
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
        .bInterfaceNumber = AUDIO_SINK_IFACE, // 3
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
        .bInterfaceNumber = AUDIO_SOURCE_IFACE, // 4
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
        .bInterfaceNumber = AUDIO_SOURCE_IFACE, // 4
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

// An interface association allows the device to group a set of interfaces to
// represent one logical device to be managed by one host driver.
static const struct usb_iface_assoc_descriptor audio_interface_association = {
    // The size of an interface association descriptor: 8
    .bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
    // A value of 11 indicates that this descriptor describes an interface
    // association.
    .bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
    // The first interface that is part of this group.
    .bFirstInterface = AUDIO_CONTROL_IFACE,
    // The number of included interfaces. This implies that the bundled
    // interfaces must be continugous.
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
    .bInterfaceCount = 3, // CONTROL + SINK, but 3 if also SOURCE
#else
    .bInterfaceCount = 2, // CONTROL + SINK, but 3 if also SOURCE
#endif
    // The class, subclass, and protocol of device represented by this
    // association. In this case a communication device.
    .bFunctionClass = USB_CLASS_AUDIO,
    .bFunctionSubClass = USB_AUDIO_SUBCLASS_CONTROL,
    .bFunctionProtocol = 0,
    // A string representing this interface. Zero means not provided.
    .iFunction = 0,
};

//
//
//
//
// --------------------- MIDI --------------------------
//
//
//

/*
 * Midi specific endpoint descriptors.
 */
static const struct usb_midi_endpoint_descriptor midi_bulk_endp[] = {{
    /* Table B-12: MIDI Adapter Class-specific Bulk OUT Endpoint
     * Descriptor
     */
    .head = {
        .bLength = sizeof(struct usb_midi_endpoint_descriptor),
        .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
        .bDescriptorSubType = USB_MIDI_SUBTYPE_MS_GENERAL,
        .bNumEmbMIDIJack = 1,
    },
    .jack[0] = {
        .baAssocJackID = 0x01,
    },
    }, {
    /* Table B-14: MIDI Adapter Class-specific Bulk IN Endpoint
     * Descriptor
     */
    .head = {
        .bLength = sizeof(struct usb_midi_endpoint_descriptor),
        .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
        .bDescriptorSubType = USB_MIDI_SUBTYPE_MS_GENERAL,
        .bNumEmbMIDIJack = 1,
    },
    .jack[0] = {
        .baAssocJackID = 0x03,
    },
} };

/*
 * Standard endpoint descriptors
 */
static const struct usb_endpoint_descriptor midi_standard_bulk_endp[] = {{
    /* Table B-11: MIDI Adapter Standard Bulk OUT Endpoint Descriptor */
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = MIDI_OUT_EP,            // 0x03
    .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 0x40,
    .bInterval = 0x00,

    .extra = &midi_bulk_endp[0],
    .extralen = sizeof(midi_bulk_endp[0])
}, {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = MIDI_IN_EP,             // 0x84
    .bmAttributes = USB_ENDPOINT_ATTR_BULK,
    .wMaxPacketSize = 0x40,
    .bInterval = 0x00,

    .extra = &midi_bulk_endp[1],
    .extralen = sizeof(midi_bulk_endp[1])
} };

/*
 * Table B-4: MIDI Adapter Class-specific AC Interface Descriptor
 */
static const struct {
    struct usb_audio_header_descriptor_head header_head;
    struct usb_audio_header_descriptor_body header_body;
} __attribute__((packed)) midi_control_functional_descriptors = {
    .header_head = {
        .bLength = sizeof(struct usb_audio_header_descriptor_head) +
            1 * sizeof(struct usb_audio_header_descriptor_body),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_AUDIO_TYPE_HEADER,
        .bcdADC = 0x0100,
        .wTotalLength =
            sizeof(struct usb_audio_header_descriptor_head) +
            1 * sizeof(struct usb_audio_header_descriptor_body),
        .binCollection = 1,
    },
    .header_body = {
        .baInterfaceNr = MIDI_STREAMING_IFACE,
    },
};

// MIDI audio control standard interface descriptor
static const struct usb_interface_descriptor midi_control_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = MIDI_CONTROL_IFACE, // 4
    .bAlternateSetting = 0,
    .bNumEndpoints = 0,
    .bInterfaceClass = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_AUDIO_SUBCLASS_CONTROL,
    .bInterfaceProtocol = 0,
    .iInterface = 0,

    .extra = &midi_control_functional_descriptors,
    .extralen = sizeof(midi_control_functional_descriptors)
} };




/*
 * Class-specific MIDI streaming interface descriptor
 */
static const struct {
    struct usb_midi_header_descriptor header;
    struct usb_midi_in_jack_descriptor in_embedded;
    struct usb_midi_in_jack_descriptor in_external;
    struct usb_midi_out_jack_descriptor out_embedded;
    struct usb_midi_out_jack_descriptor out_external;
} __attribute__((packed)) midi_streaming_functional_descriptors = {
    /* Table B-6: Midi Adapter Class-specific MS Interface Descriptor */
    .header = {
        .bLength = sizeof(struct usb_midi_header_descriptor),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_MIDI_SUBTYPE_MS_HEADER,
        .bcdMSC = 0x0100,
        .wTotalLength = sizeof(midi_streaming_functional_descriptors),
    },
    /* Table B-7: MIDI Adapter MIDI IN Jack Descriptor (Embedded) */
    .in_embedded = {
        .bLength = sizeof(struct usb_midi_in_jack_descriptor),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_MIDI_SUBTYPE_MIDI_IN_JACK,
        .bJackType = USB_MIDI_JACK_TYPE_EMBEDDED,
        .bJackID = 0x01,
        .iJack = 0x00,
    },
    /* Table B-8: MIDI Adapter MIDI IN Jack Descriptor (External) */
    .in_external = {
        .bLength = sizeof(struct usb_midi_in_jack_descriptor),
        .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
        .bDescriptorSubtype = USB_MIDI_SUBTYPE_MIDI_IN_JACK,
        .bJackType = USB_MIDI_JACK_TYPE_EXTERNAL,
        .bJackID = 0x02,
        .iJack = 0x00,
    },
    /* Table B-9: MIDI Adapter MIDI OUT Jack Descriptor (Embedded) */
    .out_embedded = {
        .head = {
            .bLength = sizeof(struct usb_midi_out_jack_descriptor),
            .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
            .bDescriptorSubtype = USB_MIDI_SUBTYPE_MIDI_OUT_JACK,
            .bJackType = USB_MIDI_JACK_TYPE_EMBEDDED,
            .bJackID = 0x03,
            .bNrInputPins = 1,
        },
        .source[0] = {
            .baSourceID = 0x02,
            .baSourcePin = 0x01,
        },
        .tail = {
            .iJack = 0x00,
        }
    },
    /* Table B-10: MIDI Adapter MIDI OUT Jack Descriptor (External) */
    .out_external = {
        .head = {
            .bLength = sizeof(struct usb_midi_out_jack_descriptor),
            .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
            .bDescriptorSubtype = USB_MIDI_SUBTYPE_MIDI_OUT_JACK,
            .bJackType = USB_MIDI_JACK_TYPE_EXTERNAL,
            .bJackID = 0x04,
            .bNrInputPins = 1,
        },
        .source[0] = {
            .baSourceID = 0x01,
            .baSourcePin = 0x01,
        },
        .tail = {
            .iJack = 0x00,
        },
    },
};

/*
 * Table B-5: MIDI Adapter Standard MS Interface Descriptor
 */
static const struct usb_interface_descriptor midi_streaming_iface[] = {{
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = MIDI_STREAMING_IFACE,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_AUDIO,
    .bInterfaceSubClass = USB_AUDIO_SUBCLASS_MIDISTREAMING,
    .bInterfaceProtocol = 0,
    .iInterface = 0,

    .endpoint = midi_standard_bulk_endp,

    // vv this murders the enumerator
    .extra = &midi_streaming_functional_descriptors,
    .extralen = sizeof(midi_streaming_functional_descriptors)
} };


static const struct usb_iface_assoc_descriptor midi_interface_association = {
    .bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
    .bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
    .bFirstInterface = MIDI_CONTROL_IFACE,
    .bInterfaceCount = 2, // control + midistreaming
    .bFunctionClass = USB_CLASS_AUDIO,
    .bFunctionSubClass = USB_AUDIO_SUBCLASS_CONTROL,
    .bFunctionProtocol = 0,
    .iFunction = 0,
};

static const struct usb_interface ifaces[] = {
#ifdef WITH_CDCACM
    {
        .num_altsetting = 1,
        .iface_assoc = &cdc_acm_interface_association,
        .altsetting = comm_iface,
    },
    {
        .num_altsetting = 1,
        .altsetting = data_iface,
    },
#endif
    {
        .num_altsetting = 1,
        .iface_assoc = &audio_interface_association,
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
    // MIDI
    {
        .num_altsetting = 1,
        .iface_assoc = &midi_interface_association,
        .altsetting = midi_control_iface,
    },
    {
        .num_altsetting = 1,
        .altsetting = midi_streaming_iface,
    }
};

const struct usb_config_descriptor usbcmp_device_config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0,   // Length of the total configuration block, 
                         // including this descriptor, in bytes.
    .bNumInterfaces = sizeof(ifaces)/sizeof(ifaces[0]), 
    .bConfigurationValue = 1,   
    .iConfiguration = 0,
    .bmAttributes = USB_CONFIG_ATTR_DEFAULT,
    .bMaxPower = 0xfa, // 500mA

    .interface = ifaces,
};

uint32_t usbcmp_nstrings(void) {
    return sizeof(usb_strings)/sizeof(usb_strings[0]);
}
