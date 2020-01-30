CFLAGS += -DTEST=1 -I..

all:	test

test:	test.o event.o adsr.o synth.o

test.o:	test.c

event.o:	../event.c
	$(CC) -c $(CFLAGS) $< -o $@

adsr.o:		../adsr.c
	$(CC) -c $(CFLAGS) $< -o $@

synth.o:	../synth.c
	$(CC) -c $(CFLAGS) $< -o $@
