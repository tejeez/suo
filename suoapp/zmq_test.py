#!/usr/bin/env python3
import zmq
ctx = zmq.Context()
s = ctx.socket(zmq.SUB)
s.connect("tcp://localhost:43700")
s.setsockopt(zmq.SUBSCRIBE, b"")
while True:
	print(s.recv())
