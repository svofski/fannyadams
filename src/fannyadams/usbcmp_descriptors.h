#pragma once

//#define WITH_MICROPHONE
#define FEEDBACK_EXPLICIT
//#define FEEDBACK_IMPLICIT

#define SOURCE_CHANNELS 				1
#if SOURCE_CHANNELS == 1
  #define SOURCE_CHANNEL_MAPPING (USB_AUDIO_CHAN_MONO)
  #define SOURCE_SAMPLE_SIZE 			2
#else
  #define SOURCE_CHANNEL_MAPPING (USB_AUDIO_CHAN_LEFTFRONT | USB_AUDIO_CHAN_RIGHTFRONT)
#define SOURCE_SAMPLE_SIZE 				4
#endif
#define SINK_SAMPLE_SIZE 				4 		// Stereo: 16 bits * 2


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

// Packet sizes must reserve space for one extra sample for rate adjustments
#define AUDIO_SINK_PACKET_SIZE 				(USB_AUDIO_PACKET_SIZE(48000,2,16) + SINK_SAMPLE_SIZE)
#define AUDIO_SOURCE_PACKET_SIZE			(USB_AUDIO_PACKET_SIZE(48000,SOURCE_CHANNELS,16) + SOURCE_SAMPLE_SIZE)

// Entity IDs for audio sink terminals: input, feature control, output
#define AUDIO_TERMINAL_INPUT 			1 	// USB Stream
#define AUDIO_VOLUME_CONTROL_ID 		2 	// Mute/Volume Control
#define AUDIO_TERMINAL_OUTPUT 			3 	// Speaker

// Entity IDs for audio source terminals: input, output
#define AUDIO_SOURCE_TERMINAL_INPUT		4 	// Line In
#define AUDIO_SOURCE_VOLUME_CONTROL		5
#define AUDIO_SOURCE_TERMINAL_OUTPUT 	6 	// USB Stream

extern uint8_t altsetting_sink;

#if defined(WITH_MICROPHONE) || defined(FEEDBACK_IMPLICIT)
extern uint8_t altsetting_source;
#endif

extern const char* usb_strings[];
extern const struct usb_device_descriptor usbcmp_device_descr;
extern const struct usb_config_descriptor usbcmp_device_config;

uint32_t usbcmp_nstrings(void);