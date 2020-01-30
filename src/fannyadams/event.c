#ifdef TEST
#include <stdio.h>
#endif

#include <inttypes.h>
#include "event.h"
#include "systick.h"

#define QUEUE_SIZE 32

static int head = 0, tail = 0;
static Event queue[QUEUE_SIZE];

void Event_Setup(void) {
    head = tail = 0;
}

int Event_QueueLength(void) {
    return (head >= tail) ? head - tail : QUEUE_SIZE + head - tail;
}

int Event_Post(Event ev) {
    if (Event_QueueLength() == QUEUE_SIZE - 1)
        return EVENT_QUEUE_FULL;

    queue[head] = ev;
    queue[head].Timestamp = Clock_Get();
    head = head + 1 == QUEUE_SIZE ? 0 : head + 1;
    return EVENT_OK;
}

Event* Event_Get(void) {
    if (Event_QueueLength() > 0) {
        int i = tail;
        (void)((++tail == QUEUE_SIZE) && (tail = 0));
        return &queue[i];
    }
    return (Event *)0;
}

Event* Event_Peek(void) {
    if (Event_QueueLength() > 0) {
        return &queue[tail];
    }
    return (Event *)0;
}

#ifdef TEST
uint32_t timestamp;
uint32_t Clock_Get()
{
    return timestamp++;
}

int event_test() {
    Event_Post((Event){EVENT_OUTPUT_PLUG, 12});
    printf("Posted event, qlen=%d\n", Event_QueueLength());
    Event* ev = Event_Get();
    printf("Got event: %d,%d qlen=%d\n", ev->EventId, ev->Data, Event_QueueLength());

    for(int i = 0; i < QUEUE_SIZE + 4; i++) {
        if (Event_Post((Event){EVENT_OUTPUT_PLUG, i}) != EVENT_OK) {
            printf("Queue overflow, len=%d\n", Event_QueueLength());
        }
    }
    printf("Purging queue\n");
    for (;Event_QueueLength() > 0;) {
        Event* ev = Event_Get();
        printf("Event: %d,%d qlen=%d\n", ev->EventId, ev->Data, Event_QueueLength());
    }
    return 0;   
}
#endif
