#!/usr/bin/env python3
# Example on use of tick messages to control transmit timing

import zmq, threading, struct

tx_ahead = int(5e6)  # How many nanoseconds before modulation the transmit frame is sent to the modulator
tx_interval = int(12.34e7)  # How often to transmit

ctx = zmq.Context()

rx = ctx.socket(zmq.SUB)
# Subscribe to both RX frames and TX tick messages
rx.connect("tcp://localhost:43300")
rx.connect("tcp://localhost:43303")
rx.setsockopt(zmq.SUBSCRIBE, b"")

tx = ctx.socket(zmq.PUB)
tx.connect("tcp://localhost:43301")

tx_next = int(0)
tx_prev = int(0)
rx_prev = int(0)

tx_data = bytes([0]*20 + [1,1,0,1,0,0,0,0,1,1,1,0,1,0,0,1,1,1,0,1,0,0] + [0]*20)

while True:
	msg = rx.recv()
	if len(msg) == 16:
		# It's a tick message
		_, _, tick = struct.unpack('IIQ', msg)
		print("\033[34m%20d: Tick" % tick)

		# Initialize timer on the first tick
		if tx_next == 0:
			tx_next = tick + tx_interval

		if tick - tx_next >= -tx_ahead:
			print("\33[1;32m%20d: TX" % tx_next)
			tx_prev = tx_next
			tx.send(struct.pack('IIQIffffffffffI', 1, 4, tx_next, 0, 0,0,0,0, 0,0,0,0,0,0, len(tx_data)) + tx_data)
			tx_next += tx_interval
	else:
		# It's a received frame
		metadata = struct.unpack('IIQIffffffffffI', msg[0:64])
		rx_time = metadata[2]
		print("\033[1;33m%20d: RX, %10d from last TX, %10d from last RX" % (rx_time, rx_time - tx_prev, rx_time - rx_prev))
		rx_prev = rx_time
