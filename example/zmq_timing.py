#!/usr/bin/env python3
# Example on use of tick messages to control transmit timing.
#
# It also works for testing how well the transmitted and received timestamps
# correspond to each other in a given modem, and for testing the minimum
# latencies reliably achievable.

import zmq, threading, struct, time

tx_ahead = int(20e6)  # How many nanoseconds before modulation the transmit frame is sent to the modulator
tx_interval = int(123.456789e6)  # How often to transmit

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
tick_prev = int(0)
ct_prev = time.time()

tx_data = bytes([0,0]*(133-11) + [1,1,0,1,0,0,0,0,1,1,1,0,1,0,0,1,1,1,0,1,0,0] + [0,0]*(255-133))

while True:
	msg = rx.recv()
	ct = time.time()  # Only used to test timing jitter of script execution
	if len(msg) == 16:
		# It's a tick message
		_, _, tick = struct.unpack('IIQ', msg)
		print("\033[34m%20d: Tick, %10d from last, %f in computer time" % (tick, tick - tick_prev, ct - ct_prev))

		# Initialize timer on the first tick
		if tx_next == 0:
			tx_next = tick + tx_interval

		if tick - tx_next >= -tx_ahead:
			print("\33[1;32m%20d: TX" % tx_next)
			tx_prev = tx_next
			tx.send(struct.pack('IIQIffffffffffI', 1, 4, tx_next, 0, 0,0,0,0, 0,0,0,0,0,0, len(tx_data)) + tx_data)
			tx_next += tx_interval

		tick_prev = tick
		ct_prev = ct
	else:
		# It's a received frame
		metadata = struct.unpack('IIQIffffffffffI', msg[0:64])
		rx_time = metadata[2]
		print("\033[1;33m%20d: RX, %10d from last TX, %10d from last RX" % (rx_time, rx_time - tx_prev, rx_time - rx_prev))
		rx_prev = rx_time
