#!/usr/bin/env python3
# Example to parse frame metadata
# and dump some bits from a frame that has not been decoded

import zmq, threading, struct
ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
rx.connect("tcp://localhost:43300")
rx.setsockopt(zmq.SUBSCRIBE, b"")

def hard_decision(data):
	return bytes([ v >= 0x80 for v in data ])

while True:
	rxframe = rx.recv()
	metadata = struct.unpack('IIQIffffffffffI', rxframe[0:64])
	rx_data = hard_decision(rxframe[64:])
	rx_timestamp = metadata[2]

	print("%9.6f s:" % (1e-9 * rx_timestamp), ' '.join(['01'[rx_data[i]] + '01'[rx_data[i+1]] for i in range(0, len(rx_data)-1, 2)]))
