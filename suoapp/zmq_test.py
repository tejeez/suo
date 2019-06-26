#!/usr/bin/env python3
import zmq
ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
rx.connect("tcp://localhost:43700")
rx.setsockopt(zmq.SUBSCRIBE, b"")

tx = ctx.socket(zmq.PUB)
tx.connect("tcp://localhost:43701")

while True:
	if rx.poll(timeout=1000):
		print(rx.recv())
	tx.send(b"test packet")
