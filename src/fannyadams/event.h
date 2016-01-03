#pragma once

typedef enum _eventId {
	EVENT_VOID = 0,
	EVENT_OUTPUT_PLUG,
	EVENT_DEBOUNCE
} EventId;

typedef struct _event {
	EventId EventId;
	uint32_t Timestamp;
	uint32_t Data;	
} Event;

#define EVENT_OK 			 0
#define EVENT_QUEUE_FULL 	-1

void Event_Setup(void);
int Event_Post(Event ev);
Event* Event_Get(void);
Event* Event_Peek(void);
int Event_QueueLength(void);