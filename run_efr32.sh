#!/bin/sh
./main < ../fsktestsignal
#rtl_sdr -f 155.4e6 -s 0.3e6 -g 50 - | ./main
