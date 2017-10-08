sleep 100000000000 > /tmp/tacnet-pipe & socat PTY,link=/tmp/tacnetp,raw STDIO </tmp/tacnet-pipe

