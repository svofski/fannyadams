#include <stdlib.h>
#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/audio.h>
#include <libopencm3/cm3/scb.h>

#include "usbcmp.h"
#include "systick.h"
#include "xprintf.h"
#include "i2s.h"

#define USB_EP0 						0x00

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
	STRID_MONO_PLAYBACK,
	STRID_MONO_RECORDING,
};

static const char * usb_strings[] = {
	"(Generic USB Audio)",
	"USB Audio Device",
	"0.1",
	"Input Terminal",
	"Output Terminal",
	"Mono Playback Input Terminal",
	"Mono Recording Input Terminal",
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
	#define AUDIO_SOURCE_EP 				(0200 | (AUDIO_SINK_EP + 1))
#endif

// Windows appears to have some slack regarding packet size, OSX is extremely touchy
#define AUDIO_SINK_PACKET_SIZE 				(USB_AUDIO_PACKET_SIZE(48000,2,16) + 4)
#define AUDIO_SOURCE_PACKET_SIZE			(USB_AUDIO_PACKET_SIZE(48000,1,16) + 4)

static uint16_t sample_ofs;
static uint8_t sample[480];

static uint8_t audio_out_buf[AUDIO_SOURCE_PACKET_SIZE];

// Entity IDs for audio sink terminals: input, feature control, output
#define AUDIO_TERMINAL_INPUT 			1 	// USB Stream
#define AUDIO_VOLUME_CONTROL_ID 		2 	// Mute/Volume Control
#define AUDIO_TERMINAL_OUTPUT 			3 	// Speaker

// Entity IDs for audio source terminals: input, output
#define AUDIO_SOURCE_TERMINAL_INPUT		4 	// Line In
#define AUDIO_SOURCE_VOLUME_CONTROL		5
#define AUDIO_SOURCE_TERMINAL_OUTPUT 	6 	// USB Stream

static uint8_t audio_sink_buffer[AUDIO_SINK_PACKET_SIZE];

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

	// microphone
	struct usb_audio_input_terminal_descriptor source_input_terminal;
	struct usb_audio_feature_unit_descriptor_head source_feature_head;
	uint8_t source_feature_body[2]; // master channel + channel
	struct usb_audio_feature_unit_descriptor_tail source_feature_tail;
	struct usb_audio_output_terminal_descriptor source_output_terminal;
} __attribute__((packed)) audio_control_functional_descriptors = {
	.header = {
		.bLength = sizeof(audio_control_functional_descriptors.header)	// header + interface nr size
			+ sizeof(audio_control_functional_descriptors.header_body),
		.bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
		.bDescriptorSubtype = USB_AUDIO_TYPE_HEADER,
		.bcdADC = 0x0100,
		.wTotalLength = sizeof(audio_control_functional_descriptors), 	// total size with terminal descriptors
		.binCollection = 2, 											// 1 streaming in + 1 streaming out
	},
	.header_body = {
		{.baInterfaceNr = AUDIO_SINK_IFACE}, 	// sink streaming interface (speaker)
		{.baInterfaceNr = AUDIO_SOURCE_IFACE},  // source streaming interface (microphone)
	},
	.input_terminal = {
	    .bLength = USB_AUDIO_INPUT_TERMINAL_DESCRIPTOR_SIZE,
	    .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
	    .bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
	    .bTerminalID = AUDIO_TERMINAL_INPUT,
	    .wTerminalType = USB_IO_TERMINAL_TYPE_STREAMING,
	    .bAssocTerminal = 0,
	    .cluster_descriptor = {
		    .bNrChannels = 1,
		    .wChannelConfig = USB_AUDIO_CHAN_MONO,
		    .iChannelNames = STRID_MONO_PLAYBACK
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

	// Microphone: input = physical, output = usb stream
	.source_input_terminal = {
	    .bLength = USB_AUDIO_INPUT_TERMINAL_DESCRIPTOR_SIZE,
	    .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
	    .bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
	    .bTerminalID = AUDIO_SOURCE_TERMINAL_INPUT,
	    .wTerminalType = USB_AUDIO_INPUT_TERMINAL_TYPE_MICROPHONE,
	    .bAssocTerminal = 0,
	    .cluster_descriptor = {
		    .bNrChannels = 1,
		    .wChannelConfig = USB_AUDIO_CHAN_MONO,
		    .iChannelNames = STRID_MONO_RECORDING,
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


static const struct usb_audio_streaming_endpoint_descriptor as_sink_endp = {
    .bLength = USB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR_SIZE,
    .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
    .bDescriptorSubtype = USB_AUDIO_EP_GENERAL,
    .bmAttributes = 0,
    .bLockDelayUnits = 1, // milliseconds
    .wLockDelay = 4, 	  // 4ms
};

static const struct usb_audio_streaming_endpoint_descriptor as_source_endp = {
    .bLength = USB_AUDIO_STREAMING_ENDPOINT_DESCRIPTOR_SIZE,
    .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
    .bDescriptorSubtype = USB_AUDIO_EP_GENERAL,
    .bmAttributes = 0,
    .bLockDelayUnits = 0,
    .wLockDelay = 0,
};

static const struct usb_endpoint_descriptor audio_sink_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = AUDIO_SINK_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_ISO_ASYNC,
	.wMaxPacketSize = AUDIO_SINK_PACKET_SIZE,
	.bInterval = 1,
	.bRefresh = 0,
	.bSynchAddress = 0,

	.extra = &as_sink_endp,
	.extralen = sizeof(as_sink_endp)
}};

static const struct usb_endpoint_descriptor audio_source_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = AUDIO_SOURCE_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_ISO_ASYNC,
	.wMaxPacketSize = AUDIO_SOURCE_PACKET_SIZE,
	.bInterval = 1,
	.bRefresh = 0,
	.bSynchAddress = 0,

	.extra = &as_source_endp,
	.extralen = sizeof(as_source_endp)
}};

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
	    .bNumEndpoints = 1,
	    .bInterfaceClass = USB_CLASS_AUDIO,
	    .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
	    .bInterfaceProtocol = 0,
	    .iInterface = 0,

		.endpoint = audio_sink_endp,

		.extra = &audio_streaming_sink_functional_descriptors,
		.extralen = sizeof(audio_streaming_sink_functional_descriptors)
	}	
};

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


static uint8_t altsetting_sink = 0;
static uint8_t altsetting_source = 0;

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
	{
	.num_altsetting = 2,
	.cur_altsetting = &altsetting_source,
	.altsetting = audio_streaming_source_iface,
	}
	};


static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0, 			// Length of the total configuration block, including this descriptor, in bytes.
	.bNumInterfaces = sizeof(ifaces)/sizeof(ifaces[0]) - 1, // disable recording interface
	.bConfigurationValue = 1, 	
	.iConfiguration = 0,
	.bmAttributes = USB_CONFIG_ATTR_DEFAULT,
	.bMaxPower = 0x32,

	.interface = ifaces,
};


/* Buffer to be used for control requests. Needs to be large to fit all those descriptors. */
static uint8_t usbd_control_buffer[256];

// static void req_set_complete_cb(usbd_device *usbd_dev, struct usb_setup_data *req) {
// 	xprintf("req_set_complete_cb - not sure what now!\n\r");
// }

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
			// (*buf)[0] = 0x33;
			// (*buf)[1] = 0x33;
			// *len = 2;
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

			//*complete = req_set_complete_cb;
			//(*buf)[0] =  USB_AUDIO_REQ_SET_CUR;
			//(*buf)[1] = 
			return USBD_REQ_HANDLED; // USBD_REQ_NEXT_CALLBACK ?
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

static void audio_data_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	if (altsetting_sink) {
		int len = usbd_ep_read_packet(usbd_dev, AUDIO_SINK_EP, audio_sink_buffer, AUDIO_SINK_PACKET_SIZE);
		int32_t* i2s_buf = I2S_GetBuffer();
		if (len == 0) {
			memset(i2s_buf, 0, AUDIO_SINK_PACKET_SIZE*2);
		} else {
			//xprintf("|iso|=%d %+6d %+6d\n\r", len, ((int16_t*)audio_buffer)[0], ((int16_t*)audio_buffer)[1]);
			xputchar('<');
			for (int i = 0; i < len/4; i++) {
				// ps = 100: 25 elements of int32
				uint32_t lr = ((int32_t*)audio_sink_buffer)[i];
				uint16_t left = lr >> 16;
				uint16_t right = lr & 0177777;
				i2s_buf[i*2] = (int32_t) ((uint32_t)left << 16);
				i2s_buf[i*2+1] = (int32_t) ((uint32_t)right << 16);
			}
			I2S_Start();
		}
	}
}

#define MIN(x,y) ((x)<(y)?(x):(y))

static void audio_data_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	if (altsetting_source) {
		// shift out current packet: this copies audio_out_buf into EP fifo
		uint16_t len = usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, audio_out_buf, AUDIO_SOURCE_PACKET_SIZE);

		// prepare the new one
		size_t avail = sizeof(audio_out_buf);
		size_t tail = 0;
		for(;avail > 0;) {
			if (sample_ofs + avail < sizeof(sample)) {
				memcpy(audio_out_buf + tail, sample + sample_ofs, avail);
				sample_ofs += avail;
				avail = 0;
			} else {
				tail = sizeof(sample) - sample_ofs;
				memcpy(audio_out_buf, sample + sample_ofs, tail);
				sample_ofs = 0;
				avail -= tail;
			}
		}
		xputchar('>');
		//xprintf(">%d o=%d;", len, sample_ofs);
	}
}


static void set_altsetting_cb(usbd_device *usbd_dev, uint16_t index, uint16_t value) {
	xprintf("set_altsetting_cb: iface=%d value=%d\r\n", index, value);
	if (index == AUDIO_SOURCE_IFACE) {
		if (value == 1) {
			xprintf("Starting to send data");
			usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, NULL, 0);
		}
	} else if (index == AUDIO_SINK_IFACE) {
		if (value == 1) {
		} else {
		}
	}
}

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;

	xprintf("set_config wValue=%d\n\r", wValue);

#ifdef WITH_CDCACM
	usbd_ep_setup(usbd_dev, CDC_BULK_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_rx_cb);
	usbd_ep_setup(usbd_dev, CDC_BULK_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(usbd_dev, CDC_COMM_EP, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);
#endif

	usbd_ep_setup(usbd_dev, AUDIO_SINK_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, AUDIO_SINK_PACKET_SIZE, audio_data_rx_cb);
	usbd_ep_setup(usbd_dev, AUDIO_SOURCE_EP, USB_ENDPOINT_ATTR_ISOCHRONOUS, AUDIO_SOURCE_PACKET_SIZE, audio_data_tx_cb);

	usbd_register_control_callback(usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE, //type
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,  //mask
				common_control_request);
}

static usbd_device *usbd_dev;

static void fill_buffer(void) {
	sample_ofs = 0;
	for (uint32_t i = 0; i < sizeof(sample); i += 2) {
		((uint16_t*)sample)[i/2] = 128*(i - sizeof(sample)/2);
	}
	sample_ofs = 0;
}

static uint32_t last = 0;

void USBCMP_Poll(void) {
	usbd_poll(usbd_dev);
	// int len = usbd_ep_read_packet(usbd_dev, AUDIO_SINK_EP, audio_buffer, AUDIO_PACKET_SIZE);
	// if (len > 0) {
	// 	xprintf("poll: len=%d\n\r", len);
	// }
	// if (altsetting_source == 1 && (Clock_Get() - last >= 8)) {
	// 	last = Clock_Get();
	// 	int len = usbd_ep_write_packet(usbd_dev, AUDIO_SOURCE_EP, audio_out_buf, AUDIO_PACKET_SIZE);
	// 	//xprintf("[%d]", len);
	// }
}

void USBCMP_Setup(void)
{
	// rcc_clock_setup_hse_3v3(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_120MHZ]);

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_OTGFS);

	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
			GPIO9 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);

	usbd_dev = usbd_init(&otgfs_usb_driver, &dev, &config,
			usb_strings, sizeof(usb_strings)/sizeof(usb_strings[0]),
			usbd_control_buffer, sizeof(usbd_control_buffer));

	usbd_register_set_altsetting_callback(usbd_dev, set_altsetting_cb);
	usbd_register_set_config_callback(usbd_dev, cdcacm_set_config);

	fill_buffer();

	xprintf("sink iface=%d source=%d\r\n", AUDIO_SINK_IFACE, AUDIO_SOURCE_IFACE);
	xprintf("Packet length=%d samples; audio buffer length=%d samples\r\n", AUDIO_SINK_PACKET_SIZE/2, AUDIO_BUFFER_SIZE);
}