#!/usr/bin/env python3
import zmq, time
ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
rx.connect("tcp://localhost:43700")
rx.setsockopt(zmq.SUBSCRIBE, b"")

tx = ctx.socket(zmq.PUB)
tx.connect("tcp://localhost:43701")

lt = time.time()
while True:
	if rx.poll(timeout=1000):
		print(rx.recv())
	t = time.time()
	if t - lt > 2.0:
		tx.send(b"test packet " * 4)
		lt = t
