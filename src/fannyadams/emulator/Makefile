LDLIBS+=-lasound 
CPPFLAGS=-DEMULATOR -I.. -O3
all:	emulator

emulator:	emulator.cpp alsamidi.cpp alsaaudio.cpp ../midi.c ../synth.c ../osc.c ../adsr.c ../notefreq.c ../patch.c
