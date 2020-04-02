#!/usr/bin/env python3
# Example to parse frame metadata and transmit the same data back
# with an incremented timestamp.

import zmq, threading, struct
ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
rx.connect("tcp://localhost:43300")
rx.setsockopt(zmq.SUBSCRIBE, b"")

tx = ctx.socket(zmq.PUB)
tx.connect("tcp://localhost:43301")

def hard_decision(data):
	return bytes([ v >= 0x80 for v in data ])

while True:
	rxframe = rx.recv()
	metadata = struct.unpack('IIQffffffffffff', rxframe[0:64])
	rx_data = rxframe[64:]
	rx_timestamp = metadata[2]

	#print(rx_timestamp, rx_data)
	tx_data = hard_decision(rx_data[12:484])
	tx_timestamp = rx_timestamp + 10000000000
	#print(tx_timestamp, tx_data)
	print(tx_timestamp, ' '.join(['o1'[tx_data[i]] + 'o1'[tx_data[i+1]] for i in range(0, len(tx_data), 2)]))

	tx.send(struct.pack('IIQffIIQ', 2, 0, tx_timestamp, 0, 1, 0, 0, len(tx_data)) + tx_data)
