#include <stdlib.h>
#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
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
#include "i2s.h"

#define SOURCE_CHANNELS 1
#if SOURCE_CHANNELS == 1
  #define SOURCE_CHANNEL_MAPPING (USB_AUDIO_CHAN_MONO)
  #define SOURCE_SAMPLE_SIZE 2
#else
  #define SOURCE_CHANNEL_MAPPING (USB_AUDIO_CHAN_LEFTFRONT | USB_AUDIO_CHAN_RIGHTFRONT)
#define SOURCE_SAMPLE_SIZE 4
#endif
//#define WITH_MICROPHONE
#define FEEDBACK_EXPLICIT
//#define FEEDBACK_IMPLICIT

#define USB_EP0 						0x00
#define STFU(x) ((void)(x))

typedef struct _AudioParams {
	int Mute;
	int Volume;
} AudioParams_T;

AudioParams_T AudioParams = {
	.Mute = 0,
	.Volume = 100,
};

enum USB_STRID {
	STRID_VENDOR = 1,
	STRID_PRODUCT,
	STRID_VERSION,
	STRID_INPUT_TERMINAL,
	STRID_OUTPUT_TERMINAL,
};

static const char * usb_strings[] = {
	"(Generic USB Audio)",
	"USB Audio Device",
	"0.1",
	"Input Terminal",
	"Output Terminal",
};

#ifdef WITH_CDCACM
	#define CDCACM_COMM_INTERFACE			0
	#define CDCACM_DATA_INTERFACE			1
	#define AUDIO_CONTROL_IFACE 			2
	#define AUDIO_SINK_IFACE 				3

	#define CDC_COMM_EP 					0x83
	#define CDC_BULK_IN_EP 					0x01
	#define CDC_BULK_OUT_EP 				0x82
	#define AUDIO_SINK_EP                   0x02
#else
	#define AUDIO_IFACE_START 				0
	#define AUDIO_EP_START 					1
	#define AUDIO_CONTROL_IFACE 			(AUDIO_IFACE_START)
	#define AUDIO_SINK_IFACE 				(AUDIO_CONTROL_IFACE+1)
	#define AUDIO_SOURCE_IFACE 				(AUDIO_SINK_IFACE+1)

	#define AUDIO_SINK_EP                   (AUDIO_EP_START)
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
	#define AUDIO_SOURCE_EP 				(0200 | (AUDIO_SINK_EP + 1))
#endif
#if defined(FEEDBACK_EXPLICIT)
	#define AUDIO_SYNCH_EP 					(0200 | (AUDIO_SINK_EP + 2))
#endif
#endif

// for 48000Hz this is 192 + 4
#define AUDIO_SINK_PACKET_SIZE 				(USB_AUDIO_PACKET_SIZE(48000,2,16) + 4)
#define AUDIO_SOURCE_PACKET_SIZE			(USB_AUDIO_PACKET_SIZE(48000,SOURCE_CHANNELS,16) + SOURCE_SAMPLE_SIZE)

// Entity IDs for audio sink terminals: input, feature control, output
#define AUDIO_TERMINAL_INPUT 			1 	// USB Stream
#define AUDIO_VOLUME_CONTROL_ID 		2 	// Mute/Volume Control
#define AUDIO_TERMINAL_OUTPUT 			3 	// Speaker

// Entity IDs for audio source terminals: input, output
#define AUDIO_SOURCE_TERMINAL_INPUT		4 	// Line In
#define AUDIO_SOURCE_VOLUME_CONTROL		5
#define AUDIO_SOURCE_TERMINAL_OUTPUT 	6 	// USB Stream

static uint8_t audio_sink_buffer[192 * 8];

static uint16_t sample_ofs;
static uint8_t sample[480];

static uint8_t audio_out_buf[AUDIO_SOURCE_PACKET_SIZE];
static usbd_device *device;

volatile uint32_t npackets_fb;
volatile uint32_t nints;
static volatile uint32_t flag;
static volatile uint32_t pid;
static volatile uint32_t flush_count;

static uint32_t npackets;
static volatile uint32_t receive_ena;
static volatile uint32_t i2s_paused;
static volatile uint32_t rxhead, rxtail, rxtop = sizeof(audio_sink_buffer);
static volatile uint16_t fb_timer_last;

static volatile uint32_t feedback_value = 48 << 14;
static volatile uint32_t sink_buffer_fullness;

#ifdef WITH_CDCACM
static char cdc_buf[64];
#endif

static const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0, 			// Device defined at Interface level
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,
	.idProduct = 0x1234,//0x5740,
	.bcdDevice = 0x0200,
	.iManufacturer = STRID_VENDOR,
	.iProduct = STRID_PRODUCT,
	.iSerialNumber = STRID_VERSION,
	.bNumConfigurations = 1,
};

// see next: usb_config_descriptor

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
	.bEndpointAddress = CDC_BULK_IN_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = CDC_BULK_OUT_EP,
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

// Class-specific AC Interface Descriptor
static const struct {
	struct usb_audio_header_descriptor_head header;
	struct usb_audio_header_descriptor_body header_body[2]; // baInterfaceNr collection (2 interfaces)
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
		.bLength = sizeof(audio_control_functional_descriptors.header)	// header + interface nr size
			+ sizeof(audio_control_functional_descriptors.header_body),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_HEADER,
		.bcdADC = 0x0100,
		.wTotalLength = sizeof(audio_control_functional_descriptors), 	// total size with terminal descriptors
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
		.binCollection = 2, 											// 1 streaming in + 1 streaming out
#else 
		.binCollection = 1, 											// 1 streaming out
#endif
	},
	.header_body = {
		{.baInterfaceNr = AUDIO_SINK_IFACE}, 	// sink streaming interface (speaker)
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
	struct usb_audio_type_i_iii_format_descriptor format; 	// Format PCM
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
    .wLockDelay = 0, //4, 	  // 4ms
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
	    .bNumEndpoints = sizeof(audio_sink_endp)/(sizeof(audio_sink_endp[0])), //2,
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

static uint8_t altsetting_sink = 0;

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
static uint8_t altsetting_source = 0;
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

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0, 			// Length of the total configuration block, including this descriptor, in bytes.
	.bNumInterfaces = sizeof(ifaces)/sizeof(ifaces[0]), 
	.bConfigurationValue = 1, 	
	.iConfiguration = 0,
	.bmAttributes = USB_CONFIG_ATTR_DEFAULT,
	.bMaxPower = 0x32,

	.interface = ifaces,
};


/* Buffer to be used for control requests. Needs to be large to fit all those descriptors. */
static uint8_t usbd_control_buffer[256];

static int common_control_request(usbd_device *usbd_dev,
	struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
	void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)buf;
	(void)usbd_dev;

	//xprintf("control_request: %x Value=%x Index=%x \n\r", req->bRequest, req->wValue, req->wIndex);

	int cs = req->wValue >> 8;
	int cn = req->wValue & 0377;
	int terminal = req->wIndex >> 8;
	int iface = req->wIndex & 0377;

	xprintf("cs=%x chan=%x entity=%x iface=%x len=%x: ", cs, cn, terminal, iface, req->wLength);

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
			return 0;
		}
		return USBD_REQ_HANDLED;

	case USB_AUDIO_REQ_GET_CUR:
		// Get current terminal setting: Value = 0x200 = Control Selector; Index = 0x201 Terminal ID and Interface
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
	}
	return 0;
}

#ifdef WITH_CDCACM
static void cdcacm_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;

	int len = usbd_ep_read_packet(usbd_dev, CDC_BULK_IN_EP, cdc_buf, 64);

	if (len) {
		while (usbd_ep_write_packet(usbd_dev, CDC_BULK_OUT_EP, cdc_buf, len) == 0);
	}
}
#endif

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

    timer_ic_disable(TIM2, TIM_IC1);  						// channel must be disabled first
    timer_disable_oc_output(TIM2, TIM_OC1); 				// disable output
    timer_ic_set_input(TIM2, TIM_IC1, TIM_IC_IN_TRC);  		// input is TRC
    timer_ic_set_prescaler(TIM2, TIM_IC1, TIM_IC_PSC_OFF); 	// prescaler off
    timer_ic_set_polarity(TIM2, TIM_IC1, TIM_IC_RISING); 	// capture rising edge
    timer_ic_enable(TIM2, TIM_IC1);  						// enable input capture channel 1


    timer_set_option(TIM2, TIM2_OR_ITR1_RMP_OTG_FS_SOF); 	// SOF is trigger 1
    timer_slave_set_mode(TIM2, TIM_SMCR_SMS_RM); 			// reset counter on trigger (SOF)
    timer_slave_set_trigger(TIM2, TIM_SMCR_TS_ITR1); 		// use trigger 1 for slaving

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
	nints++;
}

#if defined(FEEDBACK_EXPLICIT)
static void audio_synch_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	STFU(ep);
	// TODO: when the output is restarted, a callback may arrive
	// before the feedback loop is initiated from audio_data_rx_cb()
	if (npackets < 64) {
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
		npackets++;

		int avail = sizeof(audio_sink_buffer) - rxhead;
		int read = 0;
		for(;;)	{
			int len = usbd_ep_read_packet(usbd_dev, AUDIO_SINK_EP, audio_sink_buffer + rxhead, avail);
			if (len == 0) {
				break;
			}
			read += len;
			rxhead += len;
			if (rxhead == sizeof(audio_sink_buffer)) {
				rxhead = 0;
			}
		}

		sink_buffer_fullness = (rxhead >= rxtail) ? (rxhead - rxtail) : (rxhead + rxtop - rxtail);
		
		// Initiate feedback
		if (((npackets == 64) && (OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK) == pid) ||
			((npackets == 65) && (OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK) == pid)) {
			xputchar('*');
			send_explicit_fb(usbd_dev, sink_buffer_fullness);
			flag = 1;
		}

		xputchar((read == 196) ? '+' : (read == 188 ? '-' : '.'));
		if (npackets % 20 == 0) {
			xprintf("fb=%d.%03d |%d| ->%d f%d\r\n", 
					feedback_value >> 14, ((feedback_value>>4) & 0x3ff)*1000/1024, sink_buffer_fullness, 
					npackets_fb, flush_count);
		}
	}
}

#define MIN(x,y) ((x)<(y)?(x):(y))


static void audio_data_process(void) {
	size_t avail = rxhead >= rxtail ? (rxhead - rxtail) : (rxhead + rxtop - rxtail);
	if (i2s_paused && (avail < AUDIO_SINK_PACKET_SIZE * 2)) {
		return;
	}

	if (receive_ena || i2s_paused) {
		int32_t* i2s_buf = I2S_GetBuffer();
		if (receive_ena) --receive_ena;
		uint32_t i2s_buffer_ofs = 0;


		if (avail < 192) {
			xprintf("-%d %d %d %d;\r\n", avail, rxhead, rxtail, rxtop);
		} if (avail > sizeof(audio_sink_buffer) - 192) {
			xprintf("+%d %d %d %d;\r\n", avail, rxhead, rxtail, rxtop);
			rxtail += 192;
			if (rxtail >= rxtop) {
				rxtail -= rxtop;
			}
		} else {
		}

		for (size_t i = 0; i < MIN(192,avail)/2; i++) {
			uint32_t samp16 = ((uint16_t*)audio_sink_buffer)[rxtail/2];
			rxtail += 2;
			if (rxtail >= rxtop) {
				rxtail = 0;
			}
			
			i2s_buf[i2s_buffer_ofs + ((i&1) ? -1 : 1)] = samp16 << 16; // swap left & right

			i2s_buffer_ofs++;
			
			if (i2s_buffer_ofs >= AUDIO_BUFFER_SIZE) {
				i2s_buffer_ofs = 0;
			}
		}
		if (i2s_paused) {
			if (--i2s_paused == 0) {
				I2S_Start();
			}
		}
	}	
}

static void dma_cb(void) {
	receive_ena += 1;
}

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
static void audio_data_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	static size_t tosend = AUDIO_SOURCE_PACKET_SIZE - SOURCE_SAMPLE_SIZE;

	if (altsetting_source) {
		// shift out current packet: this copies audio_out_buf into EP fifo
		uint16_t len = usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, audio_out_buf, tosend);
		(void)len; // stfu

		// prepare the new one
		tosend = 48 * SOURCE_SAMPLE_SIZE;

		if (sink_buffer_fullness > AUDIO_SINK_PACKET_SIZE*2) {
			if (npackets % 16 == 0) {
				// too much, one sample less perhaps
				tosend -= SOURCE_SAMPLE_SIZE;
			}
		}
#if defined(WITH_MICROPHONE)
		size_t tocopy = tosend;
		size_t tail = 0;
		for(;tocopy > 0;) {
			if (sample_ofs + tocopy < sizeof(sample)) {
				memcpy(audio_out_buf + tail, sample + sample_ofs, tocopy);
				sample_ofs += tocopy;
				tocopy = 0;
			} else {
				tail = sizeof(sample) - sample_ofs;
				memcpy(audio_out_buf, sample + sample_ofs, tail);
				sample_ofs = 0;
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
	xprintf("set_altsetting_cb: iface=%d value=%d\r\n", index, value);
	if (index == AUDIO_SOURCE_IFACE) {
		if (value == 1) {
#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
			xprintf("Starting to send data");
			usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, NULL, 0);
#endif
		}
	} else if (index == AUDIO_SINK_IFACE) {
		if (value == 1) {
			npackets = 0;
			npackets_fb = 0;
			rxtop = sizeof(audio_sink_buffer);
			rxhead = rxtail = 0;
			receive_ena = 0;
			i2s_paused = 1;
			pid = OTG_FS_DSTS & DSTS_FNSOF_ODD_MASK;
			I2S_SetCallback(dma_cb);
			flush_synch_ep();
		} else {
			flush_synch_ep();
			I2S_SetCallback(NULL);
			I2S_Pause();
			i2s_paused = 0;
		}
	}
}

static void set_config_cb(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;

	xprintf("set_config wValue=%d\n\r", wValue);

#ifdef WITH_CDCACM
	usbd_ep_setup(usbd_dev, CDC_BULK_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
	usbd_ep_setup(usbd_dev, CDC_BULK_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(usbd_dev, CDC_COMM_EP, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);
#endif

	usbd_ep_setup(usbd_dev, AUDIO_SINK_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, AUDIO_SINK_PACKET_SIZE, audio_data_rx_cb);
#if defined(FEEDBACK_EXPLICIT)
	usbd_ep_setup(usbd_dev, AUDIO_SYNCH_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, 3, audio_synch_tx_cb);
#endif
#if defined(FEEDBACK_IMPLICIT) || defined(WITH_MICROPHONE)
	usbd_ep_setup(usbd_dev, AUDIO_SOURCE_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, AUDIO_SOURCE_PACKET_SIZE, audio_data_tx_cb);
#endif
	usbd_register_control_callback(usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE, //type
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,  //mask
				common_control_request);
}

static void fill_buffer(void) {
	sample_ofs = 0;
	for (uint32_t i = 0; i < sizeof(sample); i += 2) {
#ifdef WITH_MICROPHONE
		((uint16_t*)sample)[i/2] = 128*(i - sizeof(sample)/2);
#else
		((uint16_t*)sample)[i/2] = 0;
#endif
	}
	for (uint32_t i = 0; i < sizeof(audio_out_buf); i++) {
		audio_out_buf[i] = 0;
	}
	sample_ofs = 0;
}

void USBCMP_Poll(void) {
	usbd_poll(device);
	audio_data_process();
}

void USBCMP_Setup(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_OTGFS);

	feedback_timer_start();

	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
			GPIO9 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);

	device = usbd_init(&otgfs_usb_driver, &dev, &config,
			usb_strings, sizeof(usb_strings)/sizeof(usb_strings[0]),
			usbd_control_buffer, sizeof(usbd_control_buffer));

	usbd_register_set_altsetting_callback(device, set_altsetting_cb);
	usbd_register_set_config_callback(device, set_config_cb);

#if defined(FEEDBACK_EXPLICIT)
	usbd_register_incomplete_callback(device, incomplete);
#endif

	fill_buffer();

	xprintf("USBCMP: sink iface=%d source=%d\r\n", AUDIO_SINK_IFACE, AUDIO_SOURCE_IFACE);
	xprintf("USBCMP: Sink EP %02x |%d| Feedback type: ", AUDIO_SINK_EP, AUDIO_SINK_PACKET_SIZE);
#if defined(FEEDBACK_IMPLICIT)
	xprintf("IMPLICIT via Source endpoint %02x |%d| ", AUDIO_SOURCE_EP, AUDIO_SOURCE_PACKET_SIZE);
#else
	xprintf("EXPLICIT via Feedback endpoint %02x ", AUDIO_SYNCH_EP);
#endif
#if defined(WITH_MICROPHONE)
	xprintf("Microphone endpoint %02x |%d|", AUDIO_SOURCE_EP, AUDIO_SOURCE_PACKET_SIZE);
#else
	xprintf("No microphone");
#endif
	xprintf("\r\n");
	xprintf("USBCMP: buffer sizes: sink=|%d| ", sizeof(audio_sink_buffer));
#if defined(FEEDBACK_IMPLICIT) || defined(WITH_MICROPHONE)
	xprintf("source=|%d|", sizeof(audio_out_buf));
#endif
	xprintf("\r\n");
}