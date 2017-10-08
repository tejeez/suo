#!/bin/sh
mkfifo p0 p1 p2 p3 p4
for i in 0 1 2 3 4; do ./efr32bits < p$i 3>/tmp/tacnet-pipe & done
#./main 3>p0 4>p1 5>p2 6>p3 7>p4 < fsktestsignal
rtl_sdr -f 155.4e6 -s 0.3e6 -g 50 - | ./main 3>p0 4>p1 5>p2 6>p3 7>p4
