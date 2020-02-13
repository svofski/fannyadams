CFLAGS += -DTEST=1 -I..
LDLIBS += -lm
VPATH = ..

all:	test

test:	test.o event.o adsr.o synth.o notefreq.o midi.o osc.o patch.o

test.o:	test.c


%.o:	%.c
	$(CC) -c $(CFLAGS) $< -o $@

