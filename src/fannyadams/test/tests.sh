#!/bin/bash
make && \
if [ -z "$MLTERM" ] ; then
    mlterm --mdi=false -g 132x50 --fg=white --bg=black --fontsize 22 --deffont "3270 Condensed" -e $0 bash
else
    export GNUTERM="sixelgd size 600,250 background rgb '#000' truecolor font arial 10"
    make
    ./test
    for testtxt in *.txt ; do
        ./$testtxt
    done

    # leave this shell if it was open with starting mlterm
    if [ ! -z "$1" ] ; then 
        exec $1
    fi
fi
