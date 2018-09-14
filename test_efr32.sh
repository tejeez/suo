#!/bin/sh
mkfifo p0 p1 p2 p3 p4 tacpipe
( ./havu.py < tacpipe ) &
for i in 0 1 2 3 4; do ./efr32bits < p$i 3>tacpipe | tee -a log/log`date +%Y_%m_%d_%H_%M_%S` & done
./main 3>p0 4>p1 5>p2 6>p3 7>p4 < fsktestsignal
