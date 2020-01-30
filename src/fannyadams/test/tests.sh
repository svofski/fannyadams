#!/bin/bash
if [ -z "$MLTERM" ] ; then
    mlterm --fg=white --bg=black -e $0 bash
else
    export GNUTERM="sixelgd size 640,480 truecolor font arial 10"
    make
    ./test
    ./test_osc_1.txt
    ./test_adsr_1.txt
    if [ ! -z "$1" ] ; then 
        $1
    fi
fi
